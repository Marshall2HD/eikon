#include "pch.h"
#include "Platform/LibraryIndex.h"

#include "Core/Settings.h"
#include "Platform/ImageCodec.h"
#include "Platform/Ocr.h"

#include <fstream>
#include <mutex>
#include <thread>

namespace Eikon
{
    namespace Lib = Core::Library;
    using Core::Settings;

    namespace
    {
        constexpr auto kRescanInterval = std::chrono::seconds{ 1200 };
        // The watcher applies a burst of changes (a copy, a save with its database write) once it is quiet.
        constexpr auto kWatchSettle = std::chrono::milliseconds{ 600 };

        std::wstring NameOf(std::wstring const& path)
        {
            return std::filesystem::path{ path }.filename().wstring();
        }

        std::wstring JoinPath(std::wstring const& folder, std::wstring const& name)
        {
            return (std::filesystem::path{ folder } / name).wstring();
        }

        bool SameName(std::wstring_view a, std::wstring_view b)
        {
            return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
        }

        std::int64_t UnixFromFileTime(FILETIME const& time)
        {
            ULARGE_INTEGER value{};
            value.LowPart = time.dwLowDateTime;
            value.HighPart = time.dwHighDateTime;
            return static_cast<std::int64_t>(value.QuadPart / 10'000'000ULL) - 11'644'473'600LL;
        }

        FILETIME FileTimeFromUnix(std::int64_t seconds)
        {
            ULARGE_INTEGER value{};
            value.QuadPart = static_cast<std::uint64_t>(seconds + 11'644'473'600LL) * 10'000'000ULL;
            return FILETIME{ value.LowPart, value.HighPart };
        }

        Lib::LocalTime LocalFromUnix(std::int64_t seconds)
        {
            FILETIME const utc = FileTimeFromUnix(seconds);
            SYSTEMTIME utcTime{};
            SYSTEMTIME local{};
            if (!FileTimeToSystemTime(&utc, &utcTime) || !SystemTimeToTzSpecificLocalTime(nullptr, &utcTime, &local))
            {
                return {};
            }
            return { local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond };
        }

        std::optional<std::int64_t> UnixFromLocal(Lib::LocalTime const& time)
        {
            SYSTEMTIME local{};
            local.wYear = static_cast<WORD>(time.year);
            local.wMonth = static_cast<WORD>(time.month);
            local.wDay = static_cast<WORD>(time.day);
            local.wHour = static_cast<WORD>(time.hour);
            local.wMinute = static_cast<WORD>(time.minute);
            local.wSecond = static_cast<WORD>(time.second);
            SYSTEMTIME utc{};
            FILETIME file{};
            if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc) || !SystemTimeToFileTime(&utc, &file))
            {
                return std::nullopt;
            }
            return UnixFromFileTime(file);
        }

        struct FileInfo
        {
            std::int64_t bytes = 0;
            std::int64_t modified = 0;   // Unix seconds
        };

        std::optional<FileInfo> InfoOf(std::wstring const& path)
        {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                return std::nullopt;
            }
            FileInfo info;
            info.bytes = (static_cast<std::int64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            info.modified = UnixFromFileTime(data.ftLastWriteTime);
            return info;
        }

        std::vector<std::uint8_t> ReadAll(std::wstring const& path)
        {
            std::ifstream file{ std::filesystem::path{ path }, std::ios::binary };
            if (!file)
            {
                return {};
            }
            return { std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
        }

        // A record's image fields, without the OCR: bytes, filename, width, height, colors, basecolor and a
        // timestamp when the record has none.
        Lib::Record Analyse(std::wstring const& path, Lib::Record record)
        {
            auto const name = NameOf(path);
            auto const info = InfoOf(path);
            record[std::wstring{ Lib::kFilename }] = name;
            if (info)
            {
                record[std::wstring{ Lib::kBytes }] = std::to_wstring(info->bytes);
            }
            auto const [width, height] = LibraryFiles::PixelSize(path);
            if (width > 0 && height > 0)
            {
                record[std::wstring{ Lib::kWidth }] = std::to_wstring(width);
                record[std::wstring{ Lib::kHeight }] = std::to_wstring(height);
                // ColorMagic works on the image scaled by 0.2.
                auto const w = (std::max)(1u, static_cast<std::uint32_t>(width * 0.2));
                auto const h = (std::max)(1u, static_cast<std::uint32_t>(height * 0.2));
                auto const pixels = ImageCodec::DecodeFileScaled(path.c_str(), w, h, false);
                if (!pixels.empty())
                {
                    record[std::wstring{ Lib::kColors }] = Lib::DominantColors(pixels, w, h);
                    record[std::wstring{ Lib::kBaseColor }] = Lib::AverageColor(pixels, w, h);
                }
            }
            if (!Lib::IntField(record, Lib::kTimestamp))
            {
                std::optional<std::int64_t> seconds;
                if (auto const local = Lib::TimeFromFileName(name))
                {
                    seconds = UnixFromLocal(*local);
                }
                if (!seconds && info)
                {
                    seconds = info->modified;
                }
                if (seconds)
                {
                    record[std::wstring{ Lib::kTimestamp }] = std::to_wstring(*seconds);
                }
            }
            return record;
        }

        bool NeedsAnalysis(Lib::Record const& record, bool allowOcr)
        {
            return (allowOcr && record.find(Lib::kOcr) == record.end()) || record.find(Lib::kColors) == record.end();
        }
    }

    namespace LibraryFiles
    {
        std::pair<std::uint32_t, std::uint32_t> PixelSize(std::wstring const& path)
        {
            (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            winrt::com_ptr<IWICImagingFactory> factory;
            winrt::com_ptr<IWICBitmapDecoder> decoder;
            winrt::com_ptr<IWICBitmapFrameDecode> frame;
            UINT width = 0;
            UINT height = 0;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()))) &&
                SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put())) &&
                SUCCEEDED(decoder->GetFrame(0, frame.put())) && SUCCEEDED(frame->GetSize(&width, &height)))
            {
                return { width, height };
            }
            return { 0, 0 };
        }

        std::optional<Core::Bitmap> Decode(std::wstring const& path, double dpr)
        {
            auto const [width, height] = PixelSize(path);
            if (width == 0 || height == 0)
            {
                return std::nullopt;
            }
            Core::Bitmap bitmap;
            bitmap.width = static_cast<std::int32_t>(width);
            bitmap.height = static_cast<std::int32_t>(height);
            bitmap.dpr = dpr > 0 ? dpr : 1.0;
            bitmap.pixels = ImageCodec::DecodeFileScaled(path.c_str(), width, height, true);
            if (bitmap.IsEmpty())
            {
                return std::nullopt;
            }
            return bitmap;
        }
    }

    LibraryIndex& LibraryIndex::Current()
    {
        static LibraryIndex instance;
        return instance;
    }

    void LibraryIndex::Start()
    {
        if (m_started)
        {
            return;
        }
        m_started = true;
        m_dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        Settings::Current().Changed.Add([this](std::wstring_view key) {
            if (key == Settings::kDefaultFolder || key == Settings::kLibEnabled || key == Settings::kLibLimit)
            {
                Reload();
            }
        });
        if (m_dispatcher)
        {
            m_timer = m_dispatcher.CreateTimer();
            m_timer.Interval(kRescanInterval);
            m_timer.Tick([this](auto&&, auto&&) { Rescan(); });
            m_timer.Start();
        }
        Reload();
    }

    void LibraryIndex::Shutdown()
    {
        if (m_timer)
        {
            m_timer.Stop();
        }
        StopWatching();
        Cancel();
    }

    void LibraryIndex::Reload()
    {
        auto& settings = Settings::Current();
        if (!settings.LibEnabled())
        {
            StopWatching();
            return;
        }
        auto const folder = settings.DefaultFolder();
        if (folder != m_folder || !m_loaded || !m_watcher)
        {
            // An indexing run of the old folder keeps writing to its own database; its results are
            // dropped below (Run checks the folder).
            Cancel();
            m_folder = folder;
            m_queue.clear();
            LoadDatabase();
            Watch();
        }
        else
        {
            RebuildItems();
        }
        Scan();
        Raise();
    }

    void LibraryIndex::Rescan()
    {
        if (!Settings::Current().LibEnabled())
        {
            return;
        }
        if (!m_loaded || m_folder != Settings::Current().DefaultFolder())
        {
            Reload();
            return;
        }
        Scan();
        Raise();
    }

    void LibraryIndex::LoadDatabase()
    {
        m_records.clear();
        m_newFiles.clear();
        if (!m_folder.empty())
        {
            auto const bytes = ReadAll(JoinPath(m_folder, Lib::kDatabaseName));
            m_records = Lib::ParseDatabase(std::string_view{ reinterpret_cast<char const*>(bytes.data()), bytes.size() });
        }
        m_loaded = true;
        RebuildItems();
    }

    // "blind indexing": new screenshot files and records missing ocr/colors.
    void LibraryIndex::Scan()
    {
        m_newFiles.clear();
        if (m_folder.empty())
        {
            return;
        }
        auto& settings = Settings::Current();
        bool const nonShots = settings.LibIndexNonshots();
        bool const allowOcr = settings.LibAllowOCR();
        std::vector<std::wstring> stale;
        std::error_code error;
        std::filesystem::directory_iterator it{ std::filesystem::path{ m_folder }, error };
        if (error)
        {
            return;
        }
        std::set<std::wstring> known;
        for (auto const& record : m_records)
        {
            auto name = Lib::Field(record, Lib::kFilename);
            CharLowerBuffW(name.data(), static_cast<DWORD>(name.size()));
            known.insert(std::move(name));
        }
        for (auto const& entry : it)
        {
            if (!entry.is_regular_file(error))
            {
                continue;
            }
            auto const name = entry.path().filename().wstring();
            if (!Lib::IsImageFile(name) || (!nonShots && !Lib::IsScreenshotName(name)))
            {
                continue;
            }
            auto lower = name;
            CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
            if (!known.contains(lower))
            {
                m_newFiles.push_back(entry.path().wstring());
            }
        }
        for (auto const& record : m_records)
        {
            if (NeedsAnalysis(record, allowOcr))
            {
                auto const path = JoinPath(m_folder, Lib::Field(record, Lib::kFilename));
                if (InfoOf(path))
                {
                    stale.push_back(path);
                }
            }
        }
        if (!stale.empty())
        {
            Run(std::move(stale));
        }
    }

    void LibraryIndex::AddNewFiles()
    {
        auto files = std::move(m_newFiles);
        m_newFiles.clear();
        if (!files.empty())
        {
            Run(std::move(files));
        }
        Raise();
    }

    void LibraryIndex::Cancel()
    {
        if (m_cancel)
        {
            m_cancel->store(true);
        }
        m_queue.clear();
    }

    void LibraryIndex::RecordSaved(std::wstring const& path, Lib::Record fields)
    {
        auto& settings = Settings::Current();
        if (!settings.LibEnabled() || m_folder.empty())
        {
            return;
        }
        auto const folder = std::filesystem::path{ path }.parent_path().wstring();
        if (!SameName(folder, std::filesystem::path{ m_folder }.wstring()))
        {
            return;
        }
        auto const name = NameOf(path);
        fields[std::wstring{ Lib::kFilename }] = name;
        if (!Lib::IntField(fields, Lib::kTimestamp))
        {
            fields[std::wstring{ Lib::kTimestamp }] = std::to_wstring(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        }
        auto existing = std::find_if(m_records.begin(), m_records.end(), [&](auto const& r) { return SameName(Lib::Field(r, Lib::kFilename), name); });
        if (existing != m_records.end())
        {
            for (auto const& [key, value] : fields)
            {
                (*existing)[key] = value;
            }
        }
        else
        {
            m_records.push_back(std::move(fields));
        }
        std::erase_if(m_newFiles, [&](auto const& file) { return SameName(NameOf(file), name); });
        RebuildItems();
        SaveDatabase();
        Raise();
        Run({ path });
    }

    void LibraryIndex::Remove(std::vector<std::wstring> const& paths)
    {
        auto const before = m_records.size();
        for (auto const& path : paths)
        {
            auto const name = NameOf(path);
            std::erase_if(m_records, [&](auto const& r) { return SameName(Lib::Field(r, Lib::kFilename), name); });
            std::erase_if(m_newFiles, [&](auto const& file) { return SameName(NameOf(file), name); });
        }
        if (m_records.size() != before)
        {
            RebuildItems();
            SaveDatabase();
        }
        Raise();
    }

    void LibraryIndex::RebuildItems()
    {
        std::vector<std::shared_ptr<Item const>> items;
        items.reserve(m_records.size());
        for (auto const& record : m_records)
        {
            auto item = std::make_shared<Item>();
            item->name = Lib::Field(record, Lib::kFilename);
            if (item->name.empty())
            {
                continue;
            }
            item->path = JoinPath(m_folder, item->name);
            item->record = record;
            if (auto const seconds = Lib::IntField(record, Lib::kTimestamp))
            {
                item->timestamp = *seconds;
            }
            else if (auto const local = Lib::TimeFromFileName(item->name))
            {
                item->timestamp = UnixFromLocal(*local).value_or(0);
            }
            item->time = LocalFromUnix(item->timestamp);
            item->bytes = Lib::IntField(record, Lib::kBytes).value_or(0);
            item->width = static_cast<double>(Lib::IntField(record, Lib::kWidth).value_or(0));
            item->height = static_cast<double>(Lib::IntField(record, Lib::kHeight).value_or(0));
            items.push_back(std::move(item));
        }
        std::stable_sort(items.begin(), items.end(), [](auto const& a, auto const& b) { return a->timestamp > b->timestamp; });
        auto const limit = Settings::Current().LibLimit();
        if (limit > 0 && items.size() > static_cast<std::size_t>(limit))
        {
            items.resize(static_cast<std::size_t>(limit));
        }
        std::vector<Lib::LocalTime> times;
        std::vector<Lib::Record> records;
        for (auto const& item : items)
        {
            times.push_back(item->time);
            records.push_back(item->record);
        }
        m_facets = Lib::BuildFacets(records, times);
        m_items = std::move(items);
    }

    void LibraryIndex::SaveDatabase()
    {
        if (m_folder.empty())
        {
            return;
        }
        auto const content = Lib::EncodeDatabase(m_records);
        auto const target = JoinPath(m_folder, Lib::kDatabaseName);
        auto const temp = target + L".tmp";
        {
            std::ofstream file{ std::filesystem::path{ temp }, std::ios::binary | std::ios::trunc };
            if (!file)
            {
                return;
            }
            file.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!file)
            {
                return;
            }
        }
        if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temp.c_str());
        }
    }

    void LibraryIndex::Run(std::vector<std::wstring> files)
    {
        if (m_phase != Phase::Idle)
        {
            for (auto& file : files)
            {
                if (std::find(m_queue.begin(), m_queue.end(), file) == m_queue.end())
                {
                    m_queue.push_back(std::move(file));
                }
            }
            return;
        }
        RunAsync(std::move(files));
    }

    winrt::fire_and_forget LibraryIndex::RunAsync(std::vector<std::wstring> files)
    {
        auto dispatcher = m_dispatcher;
        if (!dispatcher || files.empty())
        {
            co_return;
        }
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        m_cancel = cancel;
        std::wstring const folder = m_folder;
        bool const allowOcr = Settings::Current().LibAllowOCR();
        m_phase = Phase::Preparing;
        m_done = 0;
        m_total = files.size();
        m_secondsLeft = -1;
        Raise();

        auto const started = std::chrono::steady_clock::now();
        auto lastRaise = started;
        for (std::size_t i = 0; i < files.size() && !cancel->load(); ++i)
        {
            auto const path = files[i];
            Lib::Record base;
            for (auto const& record : m_records)
            {
                if (SameName(Lib::Field(record, Lib::kFilename), NameOf(path)))
                {
                    base = record;
                    break;
                }
            }

            co_await winrt::resume_background();
            Lib::Record analysed;
            bool exists = InfoOf(path).has_value();
            if (exists)
            {
                analysed = Analyse(path, std::move(base));
                if (allowOcr)
                {
                    std::wstring ocr;
                    try
                    {
                        ocr = std::wstring{ co_await RecognizeTextAsync(ReadAll(path)) };
                    }
                    catch (...)
                    {
                    }
                    analysed[std::wstring{ Lib::kOcr }] = ocr;
                    analysed[std::wstring{ Lib::kDomains }] = Lib::DomainsFromText(ocr);
                }
            }
            co_await wil::resume_foreground(dispatcher);

            if (cancel->load() || folder != m_folder)
            {
                break;
            }
            if (exists)
            {
                auto const name = NameOf(path);
                auto existing = std::find_if(m_records.begin(), m_records.end(), [&](auto const& r) { return SameName(Lib::Field(r, Lib::kFilename), name); });
                if (existing != m_records.end())
                {
                    *existing = std::move(analysed);
                }
                else
                {
                    m_records.push_back(std::move(analysed));
                }
                std::erase_if(m_newFiles, [&](auto const& file) { return SameName(NameOf(file), name); });
            }
            m_phase = Phase::Indexing;
            m_done = i + 1;
            auto const now = std::chrono::steady_clock::now();
            double const elapsed = std::chrono::duration<double>(now - started).count();
            m_secondsLeft = elapsed / static_cast<double>(m_done) * static_cast<double>(m_total - m_done);
            if (now - lastRaise > std::chrono::milliseconds{ 250 } || m_done == m_total)
            {
                lastRaise = now;
                RebuildItems();
                Raise();
            }
        }

        if (folder != m_folder)
        {
            // The folder changed while indexing: the new folder's state is already loaded.
            m_phase = Phase::Idle;
            Raise();
            co_return;
        }
        m_phase = Phase::Finishing;
        Raise();
        auto const content = Lib::EncodeDatabase(m_records);
        co_await winrt::resume_background();
        {
            auto const target = JoinPath(folder, Lib::kDatabaseName);
            auto const temp = target + L".tmp";
            bool written = false;
            {
                std::ofstream file{ std::filesystem::path{ temp }, std::ios::binary | std::ios::trunc };
                if (file)
                {
                    file.write(content.data(), static_cast<std::streamsize>(content.size()));
                    written = static_cast<bool>(file);
                }
            }
            if (!written || !MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                DeleteFileW(temp.c_str());
            }
        }
        co_await wil::resume_foreground(dispatcher);
        m_phase = Phase::Idle;
        m_cancel.reset();
        RebuildItems();
        Raise();
        if (!m_queue.empty())
        {
            auto next = std::move(m_queue);
            m_queue.clear();
            Run(std::move(next));
        }
    }

    // ---- Folder watcher ----

    struct LibraryIndex::Watcher
    {
        std::wstring folder;
        wil::unique_hfile directory;
        wil::unique_event stop{ wil::EventOptions::ManualReset };
        std::thread thread;
        std::mutex lock;
        std::vector<std::pair<DWORD, std::wstring>> changes;   // FILE_ACTION_*, file name
        bool overflow = false;

        ~Watcher()
        {
            if (thread.joinable())
            {
                stop.SetEvent();
                thread.join();
            }
        }

        // Background thread: collects the folder's changes and pokes the UI thread after each batch.
        void Run(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher)
        {
            std::vector<std::byte> buffer(64 * 1024);   // operator new's alignment covers DWORD
            wil::unique_event ready{ wil::EventOptions::ManualReset };
            HANDLE const waits[]{ stop.get(), ready.get() };
            for (;;)
            {
                OVERLAPPED overlapped{};
                overlapped.hEvent = ready.get();
                ready.ResetEvent();
                if (!ReadDirectoryChangesW(directory.get(), buffer.data(), static_cast<DWORD>(buffer.size()), FALSE,
                                           FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE, nullptr,
                                           &overlapped, nullptr))
                {
                    return;
                }
                DWORD const which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                DWORD bytes = 0;
                if (which != WAIT_OBJECT_0 + 1)
                {
                    CancelIoEx(directory.get(), &overlapped);
                    GetOverlappedResult(directory.get(), &overlapped, &bytes, TRUE);
                    return;
                }
                if (!GetOverlappedResult(directory.get(), &overlapped, &bytes, FALSE))
                {
                    if (GetLastError() != ERROR_NOTIFY_ENUM_DIR)
                    {
                        return;   // the folder is gone or unreachable
                    }
                    bytes = 0;
                }
                {
                    std::lock_guard guard{ lock };
                    if (bytes == 0)
                    {
                        overflow = true;   // too many changes for the buffer: rescan
                    }
                    for (std::size_t offset = 0; bytes > 0 && offset < bytes;)
                    {
                        auto const* info = reinterpret_cast<FILE_NOTIFY_INFORMATION const*>(buffer.data() + offset);
                        changes.emplace_back(info->Action, std::wstring{ info->FileName, info->FileNameLength / sizeof(wchar_t) });
                        if (info->NextEntryOffset == 0)
                        {
                            break;
                        }
                        offset += info->NextEntryOffset;
                    }
                }
                dispatcher.TryEnqueue([] {
                    auto& index = LibraryIndex::Current();
                    if (index.m_watchTimer)
                    {
                        index.m_watchTimer.Stop();
                        index.m_watchTimer.Start();
                    }
                });
            }
        }
    };

    void LibraryIndex::Watch()
    {
        StopWatching();
        if (m_folder.empty() || !m_dispatcher)
        {
            return;
        }
        auto watcher = std::make_shared<Watcher>();
        watcher->folder = m_folder;
        watcher->directory.reset(CreateFileW(m_folder.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr));
        if (!watcher->directory)
        {
            return;
        }
        if (!m_watchTimer)
        {
            m_watchTimer = m_dispatcher.CreateTimer();
            m_watchTimer.IsRepeating(false);
            m_watchTimer.Interval(kWatchSettle);
            m_watchTimer.Tick([this](auto&&, auto&&) { ApplyFolderChanges(); });
        }
        auto dispatcher = m_dispatcher;
        watcher->thread = std::thread{ [raw = watcher.get(), dispatcher] { raw->Run(dispatcher); } };
        m_watcher = std::move(watcher);
    }

    void LibraryIndex::StopWatching()
    {
        if (m_watchTimer)
        {
            m_watchTimer.Stop();
        }
        if (auto watcher = std::move(m_watcher))
        {
            watcher->stop.SetEvent();
            if (watcher->thread.joinable())
            {
                watcher->thread.join();
            }
        }
    }

    // Files added, renamed or deleted outside Lucida (Explorer, a shell, a sync client). Deleted files
    // lose their record, renamed ones keep it under the new name, and new files that the scan would
    // count (image files with a screenshot name, any image with libIndexNonshots) get a record at
    // once and are analysed in the background like a save. Lucida's own saves and deletions come through
    // here too and find nothing left to do.
    void LibraryIndex::ApplyFolderChanges()
    {
        auto const watcher = m_watcher;
        if (!watcher || !SameName(watcher->folder, m_folder) || !m_loaded)
        {
            return;
        }
        DWORD const attributes = GetFileAttributesW(m_folder.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            return;   // the folder itself is gone or offline: keep the records
        }
        std::vector<std::pair<DWORD, std::wstring>> changes;
        bool overflow = false;
        {
            std::lock_guard guard{ watcher->lock };
            changes.swap(watcher->changes);
            std::swap(overflow, watcher->overflow);
        }
        auto const findRecord = [this](std::wstring const& name) {
            return std::find_if(m_records.begin(), m_records.end(), [&](auto const& r) { return SameName(Lib::Field(r, Lib::kFilename), name); });
        };
        bool changed = false;
        std::vector<std::wstring> added;
        std::wstring renamedFrom;
        for (auto const& [action, name] : changes)
        {
            switch (action)
            {
            case FILE_ACTION_RENAMED_OLD_NAME:
                renamedFrom = name;
                break;
            case FILE_ACTION_RENAMED_NEW_NAME:
                if (auto record = renamedFrom.empty() ? m_records.end() : findRecord(renamedFrom); record != m_records.end())
                {
                    if (Lib::IsImageFile(name) && findRecord(name) == m_records.end())
                    {
                        (*record)[std::wstring{ Lib::kFilename }] = name;
                    }
                    else
                    {
                        m_records.erase(record);
                    }
                    changed = true;
                }
                else
                {
                    added.push_back(name);
                }
                renamedFrom.clear();
                break;
            case FILE_ACTION_ADDED:
                added.push_back(name);
                break;
            default:
                break;   // removals: the existence check below; modifications only delay the batch
            }
        }

        // Records of files that are gone.
        auto const before = m_records.size();
        std::erase_if(m_records, [this](auto const& r) {
            auto const name = Lib::Field(r, Lib::kFilename);
            return !name.empty() && !InfoOf(JoinPath(m_folder, name));
        });
        changed = changed || m_records.size() != before;
        std::erase_if(m_newFiles, [](auto const& path) { return !InfoOf(path); });

        // New files.
        auto& settings = Settings::Current();
        bool const nonShots = settings.LibIndexNonshots();
        std::vector<std::wstring> fresh;
        for (auto const& name : added)
        {
            auto const path = JoinPath(m_folder, name);
            auto const info = InfoOf(path);
            if (!info || !Lib::IsImageFile(name) || (!nonShots && !Lib::IsScreenshotName(name)) || findRecord(name) != m_records.end())
            {
                continue;
            }
            Lib::Record record;
            record[std::wstring{ Lib::kFilename }] = name;
            if (!Lib::TimeFromFileName(name))
            {
                record[std::wstring{ Lib::kTimestamp }] = std::to_wstring(info->modified);
            }
            m_records.push_back(std::move(record));
            std::erase_if(m_newFiles, [&](auto const& file) { return SameName(NameOf(file), name); });
            fresh.push_back(path);
            changed = true;
        }

        if (overflow)
        {
            Scan();   // missed names: counted as new files, as a rescan would
        }
        if (changed)
        {
            RebuildItems();
            SaveDatabase();
        }
        if (changed || overflow)
        {
            Raise();
        }
        if (!fresh.empty())
        {
            Run(std::move(fresh));
        }
    }

    void LibraryIndex::Raise()
    {
        Changed();
    }
}
