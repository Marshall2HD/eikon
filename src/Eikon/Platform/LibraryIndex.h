#pragma once

// The screenshot Library's index: the records of
// imagelib.shdb in the screenshots folder, the folder scans ("blind indexing"), the analysis of each
// file (size, OCR, domains, colours) on a background thread, the 1200 s rescan and the save-time
// records, and a watcher on the folder (ReadDirectoryChangesW) that picks up files added, renamed or
// deleted outside Lucida within about a second. UI thread only, except where noted; Changed is raised
// on the UI thread.

#include "Core/Bitmap.h"
#include "Core/Event.h"
#include "Core/Library.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Eikon
{
    // Image files for the Library's actions (any thread; COM is joined as MTA when needed).
    namespace LibraryFiles
    {
        // The pixel size from the image header; {0, 0} when the file cannot be read.
        std::pair<std::uint32_t, std::uint32_t> PixelSize(std::wstring const& path);
        // The whole image as premultiplied BGRA with the given scale (the record's "dpr").
        std::optional<Core::Bitmap> Decode(std::wstring const& path, double dpr = 1.0);
    }

    class LibraryIndex
    {
    public:
        static LibraryIndex& Current();

        struct Item
        {
            std::wstring path;
            std::wstring name;
            Core::Library::Record record;
            Core::Library::LocalTime time;   // local time of the timestamp
            std::int64_t timestamp = 0;      // Unix seconds, the sort key (newest first)
            std::int64_t bytes = 0;
            double width = 0;                // pixels, 0 until known
            double height = 0;
        };

        enum class Phase
        {
            Idle,
            Preparing,   // "Preparing to index", button "Cancel"
            Indexing,    // "Indexing X of Y", button "Stop"
            Finishing,   // "Finishing indexing", while the database is written
        };

        // App start: loads the folder's database, scans it and starts the 1200 s rescan timer
        void Start();
        void Shutdown();
        // defaultFolder changed (General pane, the Library's "Select Folder"): reload and rescan.
        void Reload();
        // Ctrl+Shift+R and the timer: looks for new files and records without ocr/colors.
        void Rescan();
        // "Add Screenshots" / "Add Files": indexes the new files found by the last scan.
        void AddNewFiles();
        // "Cancel" / "Stop": ends after the current file.
        void Cancel();
        // For the output area, after a screenshot was saved to `path`: the save-time fields of
        // (app, window, dpr, mode, labels, objects; timestamp is added when missing).
        // The file is then analysed in the background like any other.
        void RecordSaved(std::wstring const& path, Core::Library::Record fields);
        // Deletes the records of these files (after they were moved to the Recycle Bin) and writes the
        // database.
        void Remove(std::vector<std::wstring> const& paths);

        std::wstring const& Folder() const noexcept { return m_folder; }
        // Newest first, at most libLimit.
        std::vector<std::shared_ptr<Item const>> const& Items() const noexcept { return m_items; }
        bool Loaded() const noexcept { return m_loaded; }
        // Screenshot files in the folder without a record (the "pre-existing" / "new images" count).
        std::size_t NewCount() const noexcept { return m_newFiles.size(); }
        Phase CurrentPhase() const noexcept { return m_phase; }
        bool IsIndexing() const noexcept { return m_phase != Phase::Idle; }
        std::size_t Done() const noexcept { return m_done; }
        std::size_t Total() const noexcept { return m_total; }
        // Estimated from the measured rate; < 0 while unknown.
        double SecondsLeft() const noexcept { return m_secondsLeft; }
        // Facets, rebuilt with the records (nothing shows them).
        Core::Library::Facets const& Facets() const noexcept { return m_facets; }

        Event<> Changed;

    private:
        LibraryIndex() = default;

        void LoadDatabase();
        void Scan();
        void RebuildItems();
        void SaveDatabase();
        void Run(std::vector<std::wstring> files);
        winrt::fire_and_forget RunAsync(std::vector<std::wstring> files);
        void Raise();
        // The folder watcher: started for m_folder by Reload, stopped by Shutdown; its changes are
        // applied on the UI thread 600 ms after the last one.
        struct Watcher;
        void Watch();
        void StopWatching();
        void ApplyFolderChanges();

        std::wstring m_folder;
        std::vector<Core::Library::Record> m_records;   // file order
        std::vector<std::shared_ptr<Item const>> m_items;
        std::vector<std::wstring> m_newFiles;
        std::vector<std::wstring> m_queue;              // files to analyse after the current run
        Core::Library::Facets m_facets;
        bool m_loaded = false;
        bool m_started = false;
        Phase m_phase = Phase::Idle;
        std::size_t m_done = 0;
        std::size_t m_total = 0;
        double m_secondsLeft = -1;
        std::shared_ptr<std::atomic<bool>> m_cancel;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{ nullptr };
        std::shared_ptr<Watcher> m_watcher;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_watchTimer{ nullptr };
    };
}
