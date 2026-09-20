#include "LibraryListActivity.h"

#include <Epub.h>
#include <FreeInkUIIcon.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryText.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/icons/search24.h"
#include "components/icons/search32.h"
#include "components/themes/vesper/VesperTheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr int SIDE_PADDING = 12;
constexpr unsigned long LONG_PRESS_MS = 1000;

constexpr int RECENT_TAB = 0;
constexpr int TITLE_TAB = 1;
constexpr int AUTHOR_TAB = 2;
constexpr int TAB_SLOTS = AUTHOR_TAB + 1;

constexpr bool isDescending(const library::SortOrder order) {
  return order == library::SortOrder::RecentDesc || order == library::SortOrder::TitleDesc ||
         order == library::SortOrder::AuthorDesc;
}

constexpr bool isRecentSort(const library::SortOrder order) {
  return order == library::SortOrder::RecentAsc || order == library::SortOrder::RecentDesc;
}

constexpr bool isAuthorSort(const library::SortOrder order) {
  return order == library::SortOrder::AuthorAsc || order == library::SortOrder::AuthorDesc;
}

constexpr library::SortOrder orderForTab(const int tab, const uint8_t descendingTabs) {
  const bool descending = (descendingTabs & (1u << tab)) != 0;
  if (tab == TITLE_TAB) return descending ? library::SortOrder::TitleDesc : library::SortOrder::TitleAsc;
  if (tab == AUTHOR_TAB) return descending ? library::SortOrder::AuthorDesc : library::SortOrder::AuthorAsc;
  return descending ? library::SortOrder::RecentDesc : library::SortOrder::RecentAsc;
}

const char* tabLabelFor(const int tab) {
  if (tab == TITLE_TAB) return tr(STR_LIBRARY_TAB_TITLE);
  if (tab == AUTHOR_TAB) return tr(STR_LIBRARY_TAB_AUTHOR);
  return tr(STR_LIBRARY_TAB_RECENT);
}

}  // namespace

LibraryListActivity::LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiTabListActivity("Library", renderer, mappedInput, true) {
  // Three short tab labels: a full-slot pill would stretch across a third of
  // the screen, so cap it at the label plus padding (slots stay put).
  tabPillMaxPad = 16;
}

void LibraryListActivity::onEnter() {
  // One lock across the base lifecycle AND the data phase: the base onEnter
  // schedules a paint, and the render task must not read the index or the
  // filter before they are in place. The rebuild also needs the lock: the
  // render task's SD-loaded fonts read glyph data at draw time, and the walk
  // needs the card to itself.
  RenderLock lock(*this);
  UiTabListActivity::onEnter();
  app.on(ACTION_SEARCH, &LibraryListActivity::searchActionTrampoline, this);

  // Recent is backed by the resident store. Prune before opening the index so
  // its persistence write never overlaps the long-lived index reader.
  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();

  // Rebuild when the index is missing, invalid, or was built with the other
  // metadata mode. Otherwise entering the screen stays instant.
  const bool readMetadata = SETTINGS.libraryUseMetadata != 0;
  const bool rebuildNeeded = !index.open(library::libraryIndexPath()) || index.header().metadataEnabled != readMetadata;
  if (rebuildNeeded) {
    index.close();
    GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
    rebuildIndex();
    if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot open library index");
  }
  degraded = index.isOpen() && index.ranksDegraded();
  if (index.isOpen() && index.dedupDegraded()) {
    LOG_ERR("LIB", "index was built without duplicate detection");
  }
  resolvePinned();

  if (usesVesperLibrary()) {
    activeTabIndex = TITLE_TAB;
    sortOrder = library::SortOrder::TitleAsc;
    descendingTabs &= static_cast<uint8_t>(~(1u << TITLE_TAB));
    query.clear();
    applyFilter();
    vesperOffset = 0;
    vesperSelected = 0;
    vesperSearchSavedOffset = 0;
    vesperSearchActive = false;
    vesperKeyboardVisible = false;
    vesperSortOpen = false;
    vesperScrollOnlyRefresh = false;
  }

  // Entered while Confirm was still held (typical when launched from the home
  // menu): ignore its release, or we would open whatever sits at row 0.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate(true);
}

void LibraryListActivity::onExit() {
  index.close();
  Activity::onExit();
}

bool LibraryListActivity::rebuildIndex() {
  library::BuildStats stats;
  const bool ok = library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0);
  if (!ok) {
    LOG_ERR("LIB", "index build failed");
    return false;
  }
  LOG_INF("LIB", "reconciled: %u unchanged, %u added, %u renamed, %u removed, %u enriched (%u dup, %u unreadable)",
          static_cast<unsigned>(stats.unchanged), static_cast<unsigned>(stats.added),
          static_cast<unsigned>(stats.renamed), static_cast<unsigned>(stats.removed),
          static_cast<unsigned>(stats.enriched), static_cast<unsigned>(stats.duplicatesDropped),
          static_cast<unsigned>(stats.unreadableSkipped));
  if (stats.dedupDegraded) LOG_ERR("LIB", "rebuild completed without duplicate detection");
  return true;
}

void LibraryListActivity::swallowHeldReleases() {
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  lockNextBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
}

int LibraryListActivity::selectedEntry() const {
  const int entry = ringPos() - 1;
  return entry < 0 ? 0 : entry;
}

// The pinned overlay applies only to the shelf that reads as "what am I up
// to": the unfiltered Recent sort, newest first. A search result is a flat
// list the reader narrowed down on purpose, and the ascending toggle asks for
// oldest-first, which pinned fresh reads would contradict.
int LibraryListActivity::pinnedCount() const {
  if (usesVesperLibrary()) return 0;
  if (activeTabIndex != RECENT_TAB || !query.empty() || !isDescending(sortOrder)) return 0;
  return pinnedTotal;
}

void LibraryListActivity::resolvePinned() {
  const auto& books = RECENT_BOOKS.getBooks();
  pinnedTotal = static_cast<uint8_t>(std::min<size_t>(books.size(), RecentBooksStore::MAX_RECENT_BOOKS));
  for (int i = 0; i < pinnedTotal; i++) pinnedAscRows[i] = 0xFFFF;
  if (pinnedTotal > 0 && index.isOpen()) {
    library::BookIdentity identities[RecentBooksStore::MAX_RECENT_BOOKS];
    for (int i = 0; i < pinnedTotal; i++) {
      const std::string& path = books[static_cast<size_t>(i)].path;
      identities[i].pathHash = library::clixPathHash(path.data(), path.size());
      // Size is only a lookup prefilter; 0 (stat failed, e.g. the index handle
      // is the card's one open reader) falls back to hash-only matching.
      identities[i].fileSize = 0;
      HalFile file;
      if (Storage.openFileForRead("LIB", path.c_str(), file)) {
        identities[i].fileSize = static_cast<uint32_t>(file.fileSize());
      }
    }
    if (!index.recentRowsFor(identities, pinnedTotal, pinnedAscRows)) {
      // Without the match the overlay would duplicate every pinned book that is
      // also in the index; better to drop the pins than to show doubles.
      LOG_ERR("LIB", "recent-book lookup failed; overlay disabled");
      pinnedTotal = 0;
    }
  }
  refreshOverlap();
}

void LibraryListActivity::refreshOverlap() {
  overlapCount = 0;
  const int total = static_cast<int>(index.bookCount());
  for (int i = 0; i < pinnedTotal; i++) {
    if (pinnedAscRows[i] == 0xFFFF || pinnedAscRows[i] >= total) continue;
    const uint16_t row =
        isDescending(sortOrder) ? static_cast<uint16_t>(total - 1 - pinnedAscRows[i]) : pinnedAscRows[i];
    overlapRows[overlapCount++] = row;
  }
  std::sort(overlapRows, overlapRows + overlapCount);
}

void LibraryListActivity::openSelectedBook() {
  std::string path;
  if (selectedEntry() < pinnedCount()) {
    const auto& books = RECENT_BOOKS.getBooks();
    if (selectedEntry() >= static_cast<int>(books.size())) return;
    path = books[static_cast<size_t>(selectedEntry())].path;
  } else {
    if (!index.isOpen()) return;
    const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(selectedEntry())));
    if (ordinal == 0xFFFF) return;

    library::ClixRecord record{};
    if (!index.readRecord(ordinal, record) || !index.readPath(record, path)) {
      LOG_ERR("LIB", "cannot resolve path for row %d", selectedEntry());
      return;
    }
  }
  // The reader screen this opens has its own surfaces; a lingering tap flash
  // would gray an unrelated element there.
  app.clearTapFlash();
  // Release the index handle first: on hardware only one reader can hold a file
  // open at a time, and the reader is about to open files of its own.
  index.close();
  onSelectBook(path);
}

void LibraryListActivity::activateIndex(const int index) {
  if (groupsCollapsed) {
    expandGroup(index);
  } else {
    openSelectedBook();
  }
}

// Row long-press prompts delete wherever grouping does not own the gesture:
// the Recent sort has no groups, and an active search is already a flat list
// the reader narrowed down on purpose ("find it, hold it, delete it").
// Unfiltered Title/Author lists keep collapse-to-groups. Pinned rows are the
// exception: holding one offers remove-from-recents, as the old Recent tab
// did.
bool LibraryListActivity::deleteEligible() const { return !groupsCollapsed && (!query.empty() || !groupable()); }

void LibraryListActivity::onRowLongPress(const int index) {
  if (index < pinnedCount()) {
    const auto& books = RECENT_BOOKS.getBooks();
    if (index < 0 || index >= static_cast<int>(books.size())) return;
    promptRemoveRecentBook(books[static_cast<size_t>(index)].path, books[static_cast<size_t>(index)].title);
  } else if (deleteEligible()) {
    promptDeleteBook(index);
  } else if (!groupsCollapsed && groupable()) {
    collapseGroups(index);
  } else {
    activateIndex(index);
  }
}

void LibraryListActivity::promptRemoveRecentBook(const std::string& path, const std::string& title) {
  const bool reopenIndex = index.isOpen();
  index.close();
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title);
  if (!confirmation) {
    LOG_ERR("LIB", "OOM: recent removal confirmation");
    if (reopenIndex && !index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    return;
  }

  startActivityForResult(std::move(confirmation), [this, path, reopenIndex](const ActivityResult& result) {
    swallowHeldReleases();
    if (reopenIndex && !index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    if (!result.isCancelled && RECENT_BOOKS.removeByPath(path)) {
      resolvePinned();
      closeRouting();
      auto& nav = activeNav();
      const int count = listCount();
      if (count == 0) {
        nav.selected = 0;
      } else if (nav.selected > count) {
        nav.selected = count;
      }
      nav.followOnBuild = true;
    }
  });
}

void LibraryListActivity::promptDeleteBook(const int entry) {
  if (!index.isOpen() || entry < 0 || entry >= bookRowCount()) return;
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  if (ordinal == 0xFFFF) return;

  std::string path;
  library::ClixRecord record{};
  if (!index.readRecord(ordinal, record) || !index.readPath(record, path)) {
    LOG_ERR("LIB", "cannot resolve path for row %d", entry);
    return;
  }
  std::string title;
  std::string author;
  rowTextFor(entry, title, author);

  // The dialog and the delete both want the card; reopen when we resume.
  index.close();
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE) + std::string("? "), title);
  if (!confirmation) {
    LOG_ERR("LIB", "OOM: delete confirmation");
    if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
    return;
  }

  startActivityForResult(std::move(confirmation), [this, path](const ActivityResult& result) {
    swallowHeldReleases();
    {
      // Same lock rationale as onEnter: the walk wants the card to itself, and
      // the render task must not read the index (or the filter) around the
      // rebuild.
      RenderLock lock(*this);
      if (!result.isCancelled) {
        LOG_DBG("LIB", "deleting %s", path.c_str());
        clearBookCache(path);
        if (!Storage.remove(path.c_str())) LOG_ERR("LIB", "cannot delete %s", path.c_str());
        if (RECENT_BOOKS.removeByPath(path)) RECENT_BOOKS.saveToFile();
        GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
        rebuildIndex();
      }
      if (!index.open(library::libraryIndexPath())) LOG_ERR("LIB", "cannot reopen library index");
      if (!result.isCancelled) {
        // Search positions, group starts, and pinned rows point into the old
        // order.
        applyFilter();
        resolvePinned();
        auto& nav = activeNav();
        const int count = listCount();
        if (count == 0) {
          nav.selected = 0;
        } else if (nav.selected > count) {
          nav.selected = count;
        }
        nav.followOnBuild = true;
      }
    }
    if (!result.isCancelled) {
      closeRouting();
      requestUpdate(true);
    }
  });
}

void LibraryListActivity::openSearch() {
  app.clearTapFlash();
  // No key filtering here on purpose. Greying out the letters that lead nowhere
  // was built, tested on device and removed: a letter you can see but cannot
  // reach reads as a broken keyboard, and the eye keeps returning to it.
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_LIBRARY_SEARCH), query, 48,
                                                           InputType::Text);
  if (!keyboard) {
    LOG_ERR("LIB", "OOM: search keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    swallowHeldReleases();
    if (result.isCancelled) return;
    query = std::get<KeyboardResult>(result.data).text;
    applyFilter();
    auto& nav = activeNav();
    if (!query.empty() && filteredCount == 0 && !degraded) {
      // Up from the tab bar reopens Search even with no results.
      nav.selected = 0;
    } else {
      // A non-empty result belongs to the list: land on
      // its first surviving row, not on the strip.
      nav.selected = 1;
    }
    nav.top = 0;
    requestUpdate();
  });
}

void LibraryListActivity::stepTab(const int direction) {
  const int next = (activeTab() + (direction > 0 ? 1 : TAB_SLOTS - 1)) % TAB_SLOTS;
  selectTab(next, false);
}

void LibraryListActivity::onTabAction(const int index) {
  app.clearTapFlash();
  selectTab(index, true);
}

void LibraryListActivity::selectTab(const int index, const bool toggleIfActive) {
  if (index < 0 || index >= TAB_SLOTS) return;
  if (toggleIfActive && index == activeTab()) descendingTabs ^= static_cast<uint8_t>(1u << index);
  sortOrder = orderForTab(index, descendingTabs);
  // The filter and the overlap rows hold positions in the old order, so they
  // must be rebuilt.
  applyFilter();
  activeTabIndex = index;
  refreshOverlap();
  // Tab changes happen only while the bar owns focus. A tab's remembered row
  // must not pull focus back into the list after the switch.
  auto& nav = activeNav();
  nav.selected = 0;
  nav.top = 0;
  requestUpdate();
}

void LibraryListActivity::toggleSortDirection() { selectTab(activeTab(), true); }

int LibraryListActivity::tabCount() const { return TAB_SLOTS; }

int LibraryListActivity::activeTab() const { return activeTabIndex; }

const char* LibraryListActivity::tabLabel(const int index) const { return tabLabelFor(index); }

fui::TabIndicator LibraryListActivity::tabIndicator(const int index) const {
  if (index != activeTab()) return fui::TabIndicator::None;
  return isDescending(sortOrder) ? fui::TabIndicator::Down : fui::TabIndicator::Up;
}

int LibraryListActivity::bookRowCount() const {
  if (!query.empty()) return static_cast<int>(filteredCount);
  // Pinned books already in the index are skipped below the pins, not doubled;
  // pinned books the index missed still show, so the difference stays split.
  const int pinned = pinnedCount();
  return static_cast<int>(index.bookCount()) + (pinned > 0 ? pinned - overlapCount : 0);
}

int LibraryListActivity::listCount() const {
  if (usesVesperLibrary()) return query.empty() ? static_cast<int>(index.bookCount()) : static_cast<int>(filteredCount);
  return groupsCollapsed ? static_cast<int>(groupCount) : bookRowCount();
}

// Entry position on screen to row position in the sort order. Identity while
// unfiltered and unpinned, so the shelf costs nothing when nothing is typed.
// With pins active, entries below pinnedCount() belong to the store and must
// not reach this; the rest walk past the pinned books' own sort rows.
int LibraryListActivity::rowFor(const int entry) const {
  if (!query.empty()) {
    if (entry < 0 || entry >= static_cast<int>(filteredCount) || !filtered) return 0;
    return filtered[entry];
  }
  const int pinned = pinnedCount();
  if (pinned == 0) return entry;
  int row = entry - pinned;
  for (int i = 0; i < overlapCount; i++) {
    if (overlapRows[i] <= row) row++;
  }
  return row;
}

bool LibraryListActivity::groupable() const { return !degraded && !isRecentSort(sortOrder) && bookRowCount() > 0; }

uint32_t LibraryListActivity::titleInitialFor(const int entry) {
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) return 0;
  return library::foldedGroupInitial(std::string_view(record.fold, record.foldLen));
}

bool LibraryListActivity::buildGroupStarts() {
  const int count = bookRowCount();
  if (count <= 0) return false;
  if (groupCapacity < count) {
    auto starts = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(count));
    if (!starts) {
      LOG_ERR("LIB", "cannot allocate %u-byte group map", static_cast<unsigned>(count * sizeof(uint16_t)));
      return false;
    }
    groupStarts = std::move(starts);
    groupCapacity = static_cast<uint16_t>(count);
  }

  groupCount = 0;
  uint32_t previousInitial = 0;
  std::string previousAuthor;
  std::string title;
  std::string author;
  previousAuthor.reserve(128);
  title.reserve(128);
  author.reserve(128);
  for (int entry = 0; entry < count; entry++) {
    bool startsGroup = entry == 0;
    if (isAuthorSort(sortOrder)) {
      rowTextFor(entry, title, author);
      startsGroup = startsGroup || author != previousAuthor;
      previousAuthor = author;
    } else {
      const uint32_t initial = titleInitialFor(entry);
      startsGroup = startsGroup || initial != previousInitial;
      previousInitial = initial;
    }
    if (startsGroup) groupStarts[groupCount++] = static_cast<uint16_t>(entry);
  }
  LOG_DBG("LIB", "group map: %u groups, %u bytes", static_cast<unsigned>(groupCount),
          static_cast<unsigned>(groupCapacity * sizeof(uint16_t)));
  return groupCount > 0;
}

int LibraryListActivity::groupForBook(const int bookEntry) const {
  int group = 0;
  while (group + 1 < groupCount && groupStarts[group + 1] <= bookEntry) group++;
  return group;
}

bool LibraryListActivity::collapseGroups(const int bookEntry) {
  if (!groupable() || !buildGroupStarts()) return false;
  expandedNav = activeNav();
  groupsCollapsed = true;
  auto& nav = activeNav();
  nav.reset(groupForBook(bookEntry) + 1);
  requestUpdate();
  return true;
}

void LibraryListActivity::expandGroup(const int groupEntry) {
  if (!groupsCollapsed || groupEntry < 0 || groupEntry >= groupCount) return;
  const int bookEntry = groupStarts[groupEntry];
  groupsCollapsed = false;
  activeNav() = expandedNav;
  auto& nav = activeNav();
  nav.selected = bookEntry + 1;
  nav.top = bookEntry;
  nav.followOnBuild = true;
  requestUpdate();
}

void LibraryListActivity::restoreExpandedList() {
  if (!groupsCollapsed) return;
  groupsCollapsed = false;
  activeNav() = expandedNav;
  requestUpdate();
}

// One pass over the sort order, keeping what matches. No index, no cache: at the
// 4096-book format cap this is 4096 comparisons of at most 96 bytes. The result
// array is allocated once with the exact upper bound and fails back to an
// explicit message rather than letting vector growth abort the firmware.
void LibraryListActivity::applyFilter() {
  groupsCollapsed = false;
  groupCount = 0;
  filtered.reset();
  filteredCount = 0;
  filterFailed = false;
  // The header shows the active query in place of the screen title, so the
  // reader can see what narrowed the list without reopening the keyboard.
  headerSearchTitle = query.empty() ? std::string() : "“" + query + "”";
  if (query.empty()) return;

  // Folded the same way the stored folds were, articles removed included —
  // otherwise "the hobbit" searches for a word no record contains.
  const std::string needle = library::fold(query, /*stripArticle=*/true);
  const int total = static_cast<int>(index.bookCount());
  if (total <= 0) return;

  auto matches = makeUniqueNoThrow<uint16_t[]>(static_cast<size_t>(total));
  if (!matches) {
    LOG_ERR("LIB", "cannot allocate %u-byte search result buffer", static_cast<unsigned>(total * sizeof(uint16_t)));
    filterFailed = true;
    return;
  }

  uint16_t matchCount = 0;
  std::string author;
  for (int row = 0; row < total; row++) {
    const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(row));
    library::ClixRecord record{};
    if (ordinal == 0xFFFF || !index.readRecord(ordinal, record)) continue;
    if (library::matchesQuery(std::string_view(record.fold, record.foldLen), needle)) {
      matches[matchCount++] = static_cast<uint16_t>(row);
      continue;
    }
    // The stored fold covers the title only, so the author has to be read and
    // folded here. That is the search most worth having: the reader who knows
    // the author usually also knows where the book is, while "emily" finding
    // Alice Hunter is the case the shelf exists to answer.
    author.clear();
    if (index.readAuthor(record, author) && library::matchesQuery(library::fold(author), needle)) {
      matches[matchCount++] = static_cast<uint16_t>(row);
      continue;
    }
    std::string fileName;
    if (index.readName(record, fileName) && library::matchesQuery(library::fold(fileName), needle)) {
      matches[matchCount++] = static_cast<uint16_t>(row);
    }
  }
  filtered = std::move(matches);
  filteredCount = matchCount;
}

void LibraryListActivity::searchActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<LibraryListActivity*>(user)->openSearch();
}

// Title and author for one entry, read straight from the index. Only ever
// called for rows about to be drawn, so at most a screenful of strings exists
// at once.
bool LibraryListActivity::rowTextFor(const int entry, std::string& title, std::string& author, std::string* fileName) {
  title.clear();
  author.clear();
  if (fileName) fileName->clear();
  if (entry < pinnedCount()) {
    const auto& books = RECENT_BOOKS.getBooks();
    if (entry < 0 || entry >= static_cast<int>(books.size())) return false;
    const auto& book = books[static_cast<size_t>(entry)];
    title = book.title;
    author = book.author;
    if (fileName) *fileName = book.path;
    return true;
  }
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  if (ordinal != 0xFFFF && index.readRecord(ordinal, record)) {
    // The build already decided both fields — from the book's own metadata when
    // it has any, and with one spelling chosen per author across the library.
    // Re-parsing the name here would throw that away, and only works while the
    // name still looks like "Title - Author".
    if (!index.readAuthor(record, author)) author.clear();
    // The stored title when the book gave one, the filename otherwise.
    if (!index.readTitle(record, title) || title.empty()) index.readName(record, title);
    if (fileName) index.readName(record, *fileName);
  }
  if (title.empty()) title = tr(STR_LIBRARY_UNKNOWN_TITLE);
  return true;
}

bool LibraryListActivity::usesVesperLibrary() const {
  return SETTINGS.uiTheme == CrossPointSettings::UI_THEME::VESPERUI;
}

int LibraryListActivity::vesperVisibleCount() const {
  if (renderer.getScreenWidth() > renderer.getScreenHeight()) return 3;  // two full + partial third
  return 4;                                                              // two columns x two rows
}

bool LibraryListActivity::pathForEntry(const int entry, std::string& path) {
  path.clear();
  if (!index.isOpen() || entry < 0 || entry >= listCount()) return false;
  const uint16_t ordinal = index.ordinalForRow(sortOrder, static_cast<uint16_t>(rowFor(entry)));
  library::ClixRecord record{};
  return ordinal != 0xFFFF && index.readRecord(ordinal, record) && index.readPath(record, path);
}

void LibraryListActivity::openVesperEntry(const int entry) {
  std::string path;
  if (!pathForEntry(entry, path)) return;
  app.clearTapFlash();
  index.close();
  onSelectBook(path);
}

void LibraryListActivity::setVesperSort(const library::SortOrder order) {
  sortOrder = order;
  if (order == library::SortOrder::TitleAsc || order == library::SortOrder::TitleDesc) {
    activeTabIndex = TITLE_TAB;
  } else if (order == library::SortOrder::AuthorAsc || order == library::SortOrder::AuthorDesc) {
    activeTabIndex = AUTHOR_TAB;
  } else {
    activeTabIndex = RECENT_TAB;
  }
  applyFilter();
  refreshOverlap();
  vesperOffset = 0;
  vesperSelected = 0;
  vesperSortOpen = false;
  requestUpdate(true);
}

void LibraryListActivity::openVesperSearch() {
  if (vesperSearchActive) {
    vesperKeyboardVisible = true;
    requestUpdate();
    return;
  }
  vesperSearchSavedOffset = vesperOffset;
  vesperSearchActive = true;
  vesperKeyboardVisible = true;
  query.clear();
  applyFilter();
  vesperOffset = 0;
  vesperSelected = 0;
  requestUpdate(true);
}

void LibraryListActivity::closeVesperSearch(const bool restorePosition) {
  if (!vesperSearchActive) return;
  query.clear();
  applyFilter();
  vesperSearchActive = false;
  vesperKeyboardVisible = false;
  if (restorePosition) vesperOffset = std::clamp(vesperSearchSavedOffset, 0, std::max(0, listCount() - 1));
  vesperSelected = vesperOffset;
  requestUpdate(true);
}

void LibraryListActivity::updateVesperSearch(const std::string& next) {
  query = next.substr(0, 48);
  applyFilter();
  vesperOffset = 0;
  vesperSelected = 0;
  requestUpdate(true);
}

void LibraryListActivity::loopVesper() {
  const int count = listCount();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const bool landscape = sw > sh;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int headerBottom = metrics.topPadding + metrics.headerHeight;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (vesperSortOpen) {
      vesperSortOpen = false;
      requestUpdate();
    } else if (vesperKeyboardVisible) {
      vesperKeyboardVisible = false;
      requestUpdate();
    } else if (vesperSearchActive) {
      closeVesperSearch(true);
    } else {
      onGoHome();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (count > 0) openVesperEntry(std::clamp(vesperSelected, 0, count - 1));
    return;
  }

  // Physical navigation follows the visible browsing axis.
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
    if (count > 0) {
      const int step = landscape ? 1 : 2;
      vesperOffset = std::min(std::max(0, count - 1), vesperOffset + step);
      vesperSelected = vesperOffset;
      vesperScrollOnlyRefresh = true;
      requestUpdate(true);
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    if (count > 0) {
      const int step = landscape ? 1 : 2;
      vesperOffset = std::max(0, vesperOffset - step);
      vesperSelected = vesperOffset;
      vesperScrollOnlyRefresh = true;
      requestUpdate(true);
    }
    return;
  }

  // Swiping is one book at a time in landscape, one row at a time in portrait.
  const auto swipe = mappedInput.wasSwipe();
  if (landscape && (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Right)) {
    if (count > 0) {
      const int delta = swipe == MappedInputManager::SwipeDir::Left ? 1 : -1;
      vesperOffset = std::clamp(vesperOffset + delta, 0, std::max(0, count - 1));
      vesperSelected = vesperOffset;
      vesperScrollOnlyRefresh = true;
      requestUpdate(true);
    }
    return;
  }
  if (!landscape && (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down)) {
    if (count > 0) {
      const int delta = swipe == MappedInputManager::SwipeDir::Up ? 2 : -2;
      vesperOffset = std::clamp(vesperOffset + delta, 0, std::max(0, count - 1));
      vesperOffset &= ~1;
      vesperSelected = vesperOffset;
      vesperScrollOnlyRefresh = true;
      requestUpdate(true);
    }
    return;
  }

  int tx = 0;
  int ty = 0;
  if (!mappedInput.wasScreenTapped(tx, ty)) return;

  // Header: Home on the left; Search and Sort sit just left of the battery.
  if (ty < headerBottom) {
    if (tx < 92) {
      onGoHome();
      return;
    }
    if (tx >= sw - 132 && tx < sw - 88) {
      if (vesperSearchActive)
        closeVesperSearch(true);
      else
        openVesperSearch();
      return;
    }
    if (tx >= sw - 88 && tx < sw - 44) {
      vesperSortOpen = !vesperSortOpen;
      vesperKeyboardVisible = false;
      requestUpdate();
      return;
    }
  }

  if (vesperSortOpen) {
    const int modalW = std::min(360, sw - 40);
    const int rowH = 44;
    const int modalH = rowH * 5 + 20;
    const int mx = (sw - modalW) / 2;
    const int my = (sh - modalH) / 2;
    if (tx >= mx && tx < mx + modalW && ty >= my + 10 && ty < my + 10 + rowH * 5) {
      const int row = (ty - (my + 10)) / rowH;
      static constexpr library::SortOrder choices[5] = {library::SortOrder::TitleAsc, library::SortOrder::TitleDesc,
                                                        library::SortOrder::AuthorAsc, library::SortOrder::AuthorDesc,
                                                        library::SortOrder::RecentDesc};
      setVesperSort(choices[std::clamp(row, 0, 4)]);
    } else {
      vesperSortOpen = false;
      requestUpdate();
    }
    return;
  }

  if (vesperSearchActive) {
    const int pillY = headerBottom + 6;
    const int pillH = 38;
    if (ty >= pillY && ty < pillY + pillH && tx >= sw - 52) {
      closeVesperSearch(true);
      return;
    }
  }

  if (vesperKeyboardVisible) {
    const int keyH = landscape ? 34 : 44;
    const int gap = 3;
    const int keyboardH = keyH * 5 + gap * 4;
    const int ky0 = sh - keyboardH - 6;
    if (ty >= ky0) {
      static const char* rows[4] = {"qwertyuiop", "asdfghjkl", "zxcvbnm", "1234567890"};
      for (int row = 0; row < 4; ++row) {
        const int countKeys = static_cast<int>(strlen(rows[row]));
        const int rowY = ky0 + row * (keyH + gap);
        if (ty < rowY || ty >= rowY + keyH) continue;
        const int pad = row == 1 ? 18 : (row == 2 ? 42 : 4);
        const int usable = sw - pad * 2;
        const int keyW = usable / countKeys;
        const int col = (tx - pad) / std::max(1, keyW);
        if (tx >= pad && col >= 0 && col < countKeys) {
          std::string next = query;
          next.push_back(rows[row][col]);
          updateVesperSearch(next);
        }
        return;
      }

      const int actionY = ky0 + 4 * (keyH + gap);
      if (ty >= actionY && ty < actionY + keyH) {
        const int third = sw / 3;
        if (tx < third) {
          if (!query.empty()) {
            std::string next = query;
            next.pop_back();
            updateVesperSearch(next);
          }
        } else if (tx < third * 2) {
          std::string next = query;
          next.push_back(' ');
          updateVesperSearch(next);
        } else {
          vesperKeyboardVisible = false;
          requestUpdate();
        }
        return;
      }
    }
  }

  // Cover hit testing mirrors renderVesper().
  const int pillReserve = vesperSearchActive ? 50 : 0;
  const int contentTop = headerBottom + 8 + pillReserve;
  const int contentBottom = sh - (landscape ? 34 : 12);
  if (landscape) {
    const int titleH = 42;
    const int coverH = std::max(120, std::min(315, contentBottom - contentTop - titleH));
    const int coverW = std::max(90, std::min(220, coverH * 2 / 3));
    const int x0 = 28;
    const int visibleThird = coverW * 3 / 4;
    const int step = std::max(coverW + 18, (sw - visibleThird - x0) / 2);
    for (int slot = 0; slot < 3; ++slot) {
      const int entry = vesperOffset + slot;
      if (entry >= count) break;
      const int x = x0 + slot * step;
      if (tx >= x && tx < std::min(sw, x + coverW) && ty >= contentTop && ty < contentTop + coverH + titleH) {
        vesperSelected = entry;
        openVesperEntry(entry);
        return;
      }
    }
  } else {
    constexpr int side = 22;
    constexpr int gapX = 18;
    constexpr int titleH = 40;
    const int colW = (sw - side * 2 - gapX) / 2;
    const int coverW = std::min(184, colW - 8);
    const int coverH = std::min(270, coverW * 3 / 2);
    const int rowH = coverH + titleH + 14;
    for (int slot = 0; slot < 4; ++slot) {
      const int entry = vesperOffset + slot;
      if (entry >= count) break;
      const int col = slot % 2;
      const int row = slot / 2;
      const int cellX = side + col * (colW + gapX);
      const int y = contentTop + row * rowH;
      if (tx >= cellX && tx < cellX + colW && ty >= y && ty < y + coverH + titleH) {
        vesperSelected = entry;
        openVesperEntry(entry);
        return;
      }
    }
  }
}

void LibraryListActivity::loop() {
  if (usesVesperLibrary()) {
    loopVesper();
    return;
  }
  UiTabListActivity::loop();
}

void LibraryListActivity::drawVesperCoverEntry(const int entry, const int x, const int y, const int width,
                                               const int height, const int titleHeight, const bool selected) {
  if (entry < 0 || entry >= listCount()) return;
  std::string title;
  std::string author;
  std::string path;
  if (!rowTextFor(entry, title, author) || !pathForEntry(entry, path)) return;

  RecentBook book;
  book.path = path;
  book.title = title;
  book.author = author;

  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    const std::string exact = epub.getThumbBmpPath(height);
    if (Storage.exists(exact.c_str()))
      book.coverBmpPath = exact;
    else if (Storage.exists(epub.getCoverBmpPath().c_str()))
      book.coverBmpPath = epub.getCoverBmpPath();
    else
      book.coverBmpPath = epub.getThumbBmpPath();
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    const std::string exact = xtc.getThumbBmpPath(height);
    if (Storage.exists(exact.c_str()))
      book.coverBmpPath = exact;
    else if (Storage.exists(xtc.getCoverBmpPath().c_str()))
      book.coverBmpPath = xtc.getCoverBmpPath();
    else
      book.coverBmpPath = xtc.getThumbBmpPath();
  }

  const Rect cover{x, y, width, height};
  if (!VesperTheme::drawBookCover(renderer, book, cover)) {
    renderer.drawRect(x, y, width, height, 1, true);
    const char* fallback = "BOOK";
    const int fw = renderer.getTextWidth(UI_10_FONT_ID, fallback, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + (width - fw) / 2, y + (height - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                      fallback, true, EpdFontFamily::BOLD);
  } else {
    renderer.drawRect(x, y, width, height, 1, true);
  }

  const int textY = y + height + 5;
  const std::string titleLine = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), width, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x, textY, titleLine.c_str(), true, EpdFontFamily::BOLD);
  if (!author.empty()) {
    const std::string authorLine = renderer.truncatedText(SMALL_FONT_ID, author.c_str(), width);
    renderer.drawText(SMALL_FONT_ID, x, textY + renderer.getLineHeight(UI_10_FONT_ID) + 1, authorLine.c_str());
  }
  if (selected) renderer.drawRect(x - 3, y - 3, width + 6, height + titleHeight + 3, 2, true);
}

void LibraryListActivity::renderVesperHeader() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const std::string homeLabel = std::string("< ") + tr(STR_HOME);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, homeLabel.c_str());

  const int iconY = metrics.topPadding + (metrics.headerHeight - 24) / 2;
  renderer.drawIcon(Search24IconBits, sw - 122, iconY, 24);

  // Sort icon: three descending horizontal strokes with a small direction cue.
  const int sx = sw - 78;
  renderer.drawLine(sx, iconY + 4, sx + 22, iconY + 4, 2, true);
  renderer.drawLine(sx + 4, iconY + 11, sx + 22, iconY + 11, 2, true);
  renderer.drawLine(sx + 8, iconY + 18, sx + 22, iconY + 18, 2, true);
}

void LibraryListActivity::renderVesperSearchPill() {
  if (!vesperSearchActive) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int y = metrics.topPadding + metrics.headerHeight + 6;
  constexpr int h = 38;
  constexpr int x = 18;
  renderer.drawRoundedRect(x, y, sw - x * 2, h, 1, h / 2, true);
  renderer.drawIcon(Search24IconBits, x + 9, y + 7, 24);
  const char* placeholder = query.empty() ? tr(STR_LIBRARY_SEARCH) : query.c_str();
  const std::string shown = renderer.truncatedText(UI_10_FONT_ID, placeholder, sw - 110);
  renderer.drawText(UI_10_FONT_ID, x + 42, y + 8, shown.c_str(), true,
                    query.empty() ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD);
  const char* close = "×";
  renderer.drawText(UI_12_FONT_ID, sw - 43, y + 6, close, true, EpdFontFamily::BOLD);
}

void LibraryListActivity::renderVesperKeyboard() {
  if (!vesperKeyboardVisible) return;
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const bool landscape = sw > sh;
  const int keyH = landscape ? 34 : 44;
  constexpr int gap = 3;
  const int keyboardH = keyH * 5 + gap * 4;
  const int ky0 = sh - keyboardH - 6;
  renderer.fillRect(0, ky0 - 3, sw, keyboardH + 9, false);
  renderer.drawLine(0, ky0 - 3, sw - 1, ky0 - 3);

  static const char* rows[4] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM", "1234567890"};
  for (int row = 0; row < 4; ++row) {
    const int n = static_cast<int>(strlen(rows[row]));
    const int pad = row == 1 ? 18 : (row == 2 ? 42 : 4);
    const int usable = sw - pad * 2;
    const int keyW = usable / n;
    const int y = ky0 + row * (keyH + gap);
    for (int col = 0; col < n; ++col) {
      const int x = pad + col * keyW;
      renderer.drawRoundedRect(x + 1, y, keyW - 2, keyH, 1, 3, true);
      char label[2] = {rows[row][col], 0};
      const int tw = renderer.getTextWidth(SMALL_FONT_ID, label, EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, x + (keyW - tw) / 2, y + (keyH - renderer.getLineHeight(SMALL_FONT_ID)) / 2,
                        label, true, EpdFontFamily::BOLD);
    }
  }

  const int actionY = ky0 + 4 * (keyH + gap);
  const int third = sw / 3;
  const char* labels[3] = {"Del", "Space", "Done"};
  for (int i = 0; i < 3; ++i) {
    renderer.drawRoundedRect(i * third + 2, actionY, third - 4, keyH, 1, 3, true);
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, labels[i], EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, i * third + (third - tw) / 2,
                      actionY + (keyH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, labels[i], true,
                      EpdFontFamily::BOLD);
  }
}

void LibraryListActivity::renderVesperSortOverlay() {
  if (!vesperSortOpen) return;
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int modalW = std::min(360, sw - 40);
  constexpr int rowH = 44;
  const int modalH = rowH * 5 + 20;
  const int mx = (sw - modalW) / 2;
  const int my = (sh - modalH) / 2;
  renderer.fillRect(mx, my, modalW, modalH, false);
  renderer.drawRect(mx, my, modalW, modalH, 2, true);

  static const char* labels[5] = {"Title A-Z", "Title Z-A", "Author A-Z", "Author Z-A", "Recently Added"};
  static constexpr library::SortOrder choices[5] = {library::SortOrder::TitleAsc, library::SortOrder::TitleDesc,
                                                    library::SortOrder::AuthorAsc, library::SortOrder::AuthorDesc,
                                                    library::SortOrder::RecentDesc};

  for (int i = 0; i < 5; ++i) {
    const int y = my + 10 + i * rowH;
    if (sortOrder == choices[i]) renderer.fillRectDither(mx + 5, y + 2, modalW - 10, rowH - 4, Color::LightGray);
    renderer.drawText(UI_10_FONT_ID, mx + 18, y + (rowH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, labels[i], true,
                      EpdFontFamily::BOLD);
    if (i < 4) renderer.drawLine(mx + 10, y + rowH - 1, mx + modalW - 11, y + rowH - 1);
  }
}

void LibraryListActivity::renderVesper() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const bool landscape = sw > sh;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int headerBottom = metrics.topPadding + metrics.headerHeight;
  const int pillReserve = vesperSearchActive ? 50 : 0;
  const int contentTop = headerBottom + 8 + pillReserve;
  const int contentBottom = sh - (landscape ? 34 : 12);
  const int count = listCount();

  // A scroll can use a true window update: leave the header physically
  // untouched and rebuild only the browsing surface + its position indicator.
  if (!vesperScrollOnlyRefresh) {
    renderer.clearScreen();
    renderVesperHeader();
    renderVesperSearchPill();
  } else {
    renderer.fillRect(0, contentTop, sw, std::max(1, contentBottom - contentTop + (landscape ? 32 : 0)), false);
  }

  if (filterFailed) {
    renderer.drawCenteredText(UI_12_FONT_ID, (contentTop + contentBottom) / 2, tr(STR_LIBRARY_SEARCH_UNAVAILABLE), true,
                              EpdFontFamily::BOLD);
  } else if (count <= 0) {
    renderer.drawCenteredText(UI_12_FONT_ID, (contentTop + contentBottom) / 2,
                              query.empty() ? tr(STR_LIBRARY_EMPTY) : tr(STR_LIBRARY_NO_RESULTS), true,
                              EpdFontFamily::BOLD);
  } else if (landscape) {
    constexpr int titleH = 42;
    const int coverH = std::max(120, std::min(315, contentBottom - contentTop - titleH));
    const int coverW = std::max(90, std::min(220, coverH * 2 / 3));
    const int x0 = 28;
    const int visibleThird = coverW * 3 / 4;
    const int step = std::max(coverW + 18, (sw - visibleThird - x0) / 2);

    renderer.setClipRect(0, contentTop, sw, std::max(1, contentBottom - contentTop));
    for (int slot = 0; slot < 3; ++slot) {
      const int entry = vesperOffset + slot;
      if (entry >= count) break;
      drawVesperCoverEntry(entry, x0 + slot * step, contentTop, coverW, coverH, titleH, entry == vesperSelected);
    }
    renderer.setClipRect(0, 0, sw, sh);

    // Horizontal position indicator. The number is the leftmost full book.
    constexpr int trackX = 32;
    const int readoutW = 78;
    const int trackRight = sw - readoutW - 22;
    const int trackY = sh - 18;
    renderer.drawLine(trackX, trackY, trackRight, trackY, 1, true);
    const int trackW = std::max(1, trackRight - trackX);
    const int thumbW = std::max(24, trackW * std::min(2, count) / std::max(1, count));
    const int maxOffset = std::max(1, count - 1);
    const int thumbX = trackX + (trackW - thumbW) * std::clamp(vesperOffset, 0, maxOffset) / maxOffset;
    renderer.fillRect(thumbX, trackY - 2, thumbW, 5, true);
    char pos[24];
    snprintf(pos, sizeof(pos), "%d / %d", std::min(count, vesperOffset + 1), count);
    renderer.drawText(SMALL_FONT_ID, sw - readoutW, sh - 25, pos, true, EpdFontFamily::BOLD);
  } else {
    constexpr int side = 22;
    constexpr int gapX = 18;
    constexpr int titleH = 40;
    const int colW = (sw - side * 2 - gapX) / 2;
    const int coverW = std::min(184, colW - 8);
    const int coverH = std::min(270, coverW * 3 / 2);
    const int rowH = coverH + titleH + 14;

    for (int slot = 0; slot < 4; ++slot) {
      const int entry = vesperOffset + slot;
      if (entry >= count) break;
      const int col = slot % 2;
      const int row = slot / 2;
      const int cellX = side + col * (colW + gapX);
      const int x = cellX + (colW - coverW) / 2;
      const int y = contentTop + row * rowH;
      if (y + coverH > contentBottom) break;
      drawVesperCoverEntry(entry, x, y, coverW, coverH, titleH, entry == vesperSelected);
    }

    // Portrait uses only a vertical scroll indicator, no count.
    if (count > 4) {
      const int trackX = sw - 7;
      const int trackY = contentTop;
      const int trackH = std::max(1, contentBottom - contentTop);
      renderer.drawLine(trackX, trackY, trackX, trackY + trackH - 1, 1, true);
      const int thumbH = std::max(30, trackH * 4 / count);
      const int maxOffset = std::max(1, count - 4);
      const int thumbY = trackY + (trackH - thumbH) * std::clamp(vesperOffset, 0, maxOffset) / maxOffset;
      renderer.fillRect(trackX - 1, thumbY, 3, thumbH, true);
    }
  }

  if (!vesperScrollOnlyRefresh) {
    renderVesperSortOverlay();
    renderVesperKeyboard();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayWindow(0, contentTop, sw, std::max(1, sh - contentTop));
    vesperScrollOnlyRefresh = false;
  }
}

void LibraryListActivity::render(RenderLock&& lock) {
  if (usesVesperLibrary()) {
    renderVesper();
    return;
  }
  UiTabListActivity::render(std::move(lock));
}

bool LibraryListActivity::handleCustomInput() {
  if (lockNextConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lockNextConfirmRelease = false;
    return true;
  }
  if (lockNextBackRelease && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockNextBackRelease = false;
    return true;
  }

  return false;
}

bool LibraryListActivity::handleButtons() {
  const int count = listCount();
  auto& nav = activeNav();

  // Every hold action fires at the threshold, mid-hold, including the ones
  // that open a dialog (remove-recent, delete). The release that follows is
  // armed as suppressed by wasLongPressed() and consumed globally by
  // ActivityManager::loop() before any activity runs, so it cannot land in
  // the freshly opened confirmation and select its default.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, LONG_PRESS_MS)) {
    if (tabsFocused()) {
      if (!degraded) toggleSortDirection();
    } else if (selectedEntry() < pinnedCount()) {
      const auto& books = RECENT_BOOKS.getBooks();
      if (selectedEntry() < static_cast<int>(books.size())) {
        const auto& book = books[static_cast<size_t>(selectedEntry())];
        promptRemoveRecentBook(book.path, book.title);
      }
    } else if (deleteEligible()) {
      if (count > 0) promptDeleteBook(selectedEntry());
    } else if (!groupsCollapsed && groupable()) {
      collapseGroups(selectedEntry());
    } else {
      activateIndex(selectedEntry());
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!query.empty()) {
      query.clear();
      applyFilter();
      nav.selected = 0;
      nav.top = 0;
      requestUpdate();
    } else if (groupsCollapsed) {
      restoreExpandedList();
    } else if (!tabsFocused() && !degraded) {
      // Keep the current list and viewport while returning focus to the tabs.
      nav.selected = 0;
      requestUpdate();
    } else {
      onGoHome();
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (tabsFocused()) {
      stepTab(1);
      return true;
    }
    if (count > 0) activateIndex(selectedEntry());
    return true;
  }

  return false;
}

void LibraryListActivity::navigateButtons() {
  const int count = listCount();
  auto& nav = activeNav();
  buttonNavigator.onNextRelease([this, count] {
    if (count > 0) moveRingTo(ringPos() == count ? 1 : ringPos() + 1);
  });
  buttonNavigator.onPreviousRelease([this, count] {
    if (tabsFocused() && !degraded) {
      openSearch();
    } else if (count > 0) {
      moveRingTo(ringPos() <= 1 ? count : ringPos() - 1);
    }
  });
  // A held button steps tabs while the strip has focus (the base behaviour
  // Settings keeps) and page-jumps once the selection is down in the rows,
  // where fast travel through a long shelf is what a hold means.
  buttonNavigator.onNextContinuous([this, count, &nav] {
    if (tabsFocused()) {
      stepTab(1);
    } else if (count > 0) {
      moveRingTo(ButtonNavigator::nextPageIndex(selectedEntry(), count, nav.pageRows()) + 1);
    }
  });
  buttonNavigator.onPreviousContinuous([this, count, &nav] {
    if (tabsFocused()) {
      stepTab(-1);
    } else if (count > 0) {
      moveRingTo(ButtonNavigator::previousPageIndex(selectedEntry(), count, nav.pageRows()) + 1);
    }
  });
}

void LibraryListActivity::buildRows(UiScreen& screen) {
  auto& nav = activeNav();
  const int count = listCount();
  const bool authorGrouped = isAuthorSort(sortOrder);
  const bool grouped = !isRecentSort(sortOrder);

  fui::ListProps props;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 1;
  // Breathing room between rows; the dense theme default packs the two-line
  // rows edge-to-edge.
  props.rowGap = std::max<int16_t>(screen.theme().listRowGap, 6);
  props.headerUnderline = false;
  syncTabListViewport(screen, props);

  // Keep one extra entry in the reusable window for a clipped trailing row.
  const size_t cap = static_cast<size_t>(nav.visibleRows > 0 ? nav.visibleRows : 1) + 1;
  if (winTitles.size() < cap) winTitles.resize(cap);
  if (winAuthors.size() < cap) winAuthors.resize(cap);
  if (!groupsCollapsed && winHeaders.size() < cap) winHeaders.resize(cap);
  winItems.clear();
  if (winItems.capacity() < cap) winItems.reserve(cap);

  int rows = 0;
  int headers = 0;
  uint32_t previousInitial = 0;
  std::string rowFile;
  rowFile.reserve(128);
  // Capture this after syncTabListViewport(), which may clamp nav.top.
  const int windowStart = static_cast<int>(props.topIndex);
  for (int entry = windowStart; entry < count && rows < static_cast<int>(cap); entry++) {
    std::string& title = winTitles[static_cast<size_t>(rows)];
    std::string& author = winAuthors[static_cast<size_t>(rows)];
    fui::ListItem item;
    if (groupsCollapsed) {
      const int bookEntry = groupStarts[entry];
      if (authorGrouped) {
        rowTextFor(bookEntry, title, author);
        formatAuthorHeading(author, title);
      } else {
        formatInitialHeading(titleInitialFor(bookEntry), title);
      }
    } else {
      if (!rowTextFor(entry, title, author, &rowFile)) continue;
      uint32_t initial = 0;
      bool startsGroup = false;
      if (authorGrouped) {
        startsGroup = rows == 0 || author != winAuthors[static_cast<size_t>(rows - 1)];
      } else if (grouped) {
        initial = titleInitialFor(entry);
        startsGroup = rows == 0 || initial != previousInitial;
        previousInitial = initial;
      }
      if (startsGroup) {
        std::string& heading = winHeaders[static_cast<size_t>(headers++)];
        if (authorGrouped)
          formatAuthorHeading(author, heading);
        else
          formatInitialHeading(initial, heading);
        item.sectionHeading = heading.c_str();
      }
      if (!authorGrouped && !author.empty()) item.subtitle = author.c_str();
    }

    item.label = title.c_str();
    // Group headings stay bare; every book row gets its file-type icon.
    if (!groupsCollapsed && !rowFile.empty()) item.icon = listIconFor(UITheme::getFileIcon(rowFile), 32);
    item.actionValue = static_cast<int16_t>(entry);
    winItems.push_back(item);
    rows++;
  }

  props.items = winItems.data();
  props.itemsWindowFirst = static_cast<uint16_t>(windowStart);
  props.itemsWindowCount = static_cast<uint16_t>(winItems.size());
  screen.list(props);
  const int next = nav.drawnRows;
  const auto body = screen.body();
  LOG_DBG("LIB", "page tab=%d top=%d full=%d loaded=%d body=%d..%d next=%d title=%s", activeTabIndex, windowStart,
          nav.drawnRows, rows, body.y, body.bottom(),
          next < rows ? winItems[static_cast<size_t>(next)].actionValue : -1,
          next < rows ? winItems[static_cast<size_t>(next)].label : "<none>");
  LOG_DBG("LIB", "page first=%d title=%s", rows > 0 ? winItems[0].actionValue : -1,
          rows > 0 ? winItems[0].label : "<none>");
}

void LibraryListActivity::formatInitialHeading(uint32_t initial, std::string& out) {
  out.clear();
  if (initial == 0) {
    out.push_back('#');
    return;
  }
  if (initial >= 'a' && initial <= 'z') initial -= 'a' - 'A';
  utf8AppendCodepoint(initial, out);
}

void LibraryListActivity::formatAuthorHeading(const std::string& author, std::string& out) const {
  out = author.empty() ? std::string(tr(STR_LIBRARY_UNKNOWN_AUTHOR)) : author;
  if (author.empty()) return;
  const size_t lastSpace = out.find_last_of(' ');
  if (lastSpace != std::string::npos && lastSpace + 1 < out.size()) {
    out = out.substr(lastSpace + 1) + ", " + out.substr(0, lastSpace);
  }
}

void LibraryListActivity::buildHeader(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  fui::HeaderProps header;
  header.title = headerTitle();
  header.titleText = theme.titleText;
  header.titleText.align = theme.headerTitleAlign;
  header.sidePadding = theme.headerSidePadding;
  header.minTouchSize = theme.minTouchSize;
  header.styles = theme.popup;
  if (header.styles.normal.border.kind == fui::PaintKind::None && theme.headerUnderline > 0) {
    header.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    header.styles.normal.borderWidth = theme.headerUnderline;
  }
  header.trailingStyles = fui::plainStyles(fui::Paint::solid(fui::Color::Black));
  header.borderEdges = fui::EdgeBottom;
  if (!degraded) {
    header.trailingIcon = fui::bitmapFromIcon(icon_search_32);
    header.trailingAction = ACTION_SEARCH;
    const int titleFontId = uiScaleSpec().titleFontId;
    header.actionOffsetY =
        static_cast<int16_t>((renderer.getLineHeight(titleFontId) - renderer.getTextHeight(titleFontId)) / 2);
  }
  const auto frameRect = screen.frame().screen();
  // Header and tabs share a screen-relative boundary, independent of bezel insets.
  fui::header(screen.frame(),
              fui::Rect{frameRect.x, static_cast<int16_t>(metrics.topPadding), frameRect.width,
                        static_cast<int16_t>(metrics.headerHeight)},
              header);
}

void LibraryListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // The position readout owns the line above the hints; rows must not overlap
  // it.
  const int16_t readoutReserved = static_cast<int16_t>(renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing);
  buildHeader(screen);
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight + readoutReserved), 0});

  if (!degraded) buildTabBar(screen);
  if (bookRowCount() == 0) {
    const char* message = tr(STR_LIBRARY_NO_RESULTS);
    if (filterFailed) {
      message = tr(STR_LIBRARY_SEARCH_UNAVAILABLE);
    } else if (query.empty()) {
      message = tr(STR_LIBRARY_EMPTY);
    }
    screen.centeredText(message);
    return;
  }
  buildRows(screen);
}

// "12/69 books" at the bottom right: which book is selected, out of how many.
//
// NOT a page count. How many rows fit varies with the view (author headings
// consume band height), so a page total grows and shrinks as you scroll. The
// book position is stable by construction, and it answers the question the
// reader actually has: how far in am I, and how much is left.
void LibraryListActivity::drawPositionReadout() const {
  const int count = listCount();
  if (count <= 0) return;

  char buf[32];
  const char* positionFormat = groupsCollapsed ? tr(STR_LIBRARY_GROUP_POSITION) : tr(STR_LIBRARY_POSITION);
  snprintf(buf, sizeof(buf), positionFormat, selectedEntry() + 1, count);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getTextWidth(SMALL_FONT_ID, buf);
  const int x = renderer.getScreenWidth() - width - SIDE_PADDING;
  const int y = renderer.getScreenHeight() - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, x, y, buf, true);
}

const char* LibraryListActivity::headerTitle() const {
  if (!headerSearchTitle.empty()) return headerSearchTitle.c_str();
  return degraded ? tr(STR_LIBRARY_TITLE_UNSORTED) : tr(STR_LIBRARY);
}

void LibraryListActivity::drawHoldHelp() const {
  if (mappedInput.hasTouch() || groupsCollapsed) return;
  const char* help = nullptr;
  if (tabsFocused() && !degraded)
    help = tr(STR_LIBRARY_HOLD_SORT);
  else if (!tabsFocused() && selectedEntry() < pinnedCount())
    help = tr(STR_HOLD_OPEN_TO_REMOVE);  // pinned recents: hold removes from the list
  else if (!tabsFocused() && deleteEligible() && listCount() > 0)
    help = tr(STR_HOLD_OPEN_TO_DELETE);
  else if (!tabsFocused() && groupable())
    help = tr(STR_LIBRARY_HOLD_GROUPS);
  if (!help) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int y = renderer.getScreenHeight() - metrics.buttonHintsHeight - lineHeight;
  GUI.drawHelpText(renderer, Rect{SIDE_PADDING, y, renderer.getScreenWidth() / 2 - SIDE_PADDING, lineHeight}, help);
}

void LibraryListActivity::drawFooter() {
  drawPositionReadout();
  drawHoldHelp();

  const bool backGoesHome = tabsFocused() && !groupsCollapsed && query.empty();
  const char* backLabel = backGoesHome ? tr(STR_HOME) : tr(STR_BACK);
  const char* confirmLabel = groupsCollapsed ? tr(STR_SELECT) : tr(STR_OPEN);
  const bool canSearch = tabsFocused() && !degraded;
  const auto labels = mappedInput.mapLabels(backLabel, tabsFocused() ? tr(STR_TOGGLE) : confirmLabel,
                                            canSearch ? tr(STR_SEARCH) : tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
