
struct IPCServer : wxServer {
    wxConnectionBase *OnAcceptConnection(const wxString &topic) {
        sys->frame->DeIconize();
        if (topic.Len() && topic != L"*") sys->Open(topic);
        return new wxConnection();
    }
};

struct MyApp : wxApp {
    MyFrame *frame {nullptr};
    unique_ptr<IPCServer> serv {make_unique<IPCServer>()};
    wxString filename;
    bool initateventloop {false};
    wxLocale locale;
    unique_ptr<wxSingleInstanceChecker> instance_checker {nullptr};

    void AddTranslation(const wxString &basepath) {
        #ifdef __WXGTK__
            locale.AddCatalogLookupPathPrefix(L"/usr");
            locale.AddCatalogLookupPathPrefix(L"/usr/local");
            #ifdef LOCALEDIR
                locale.AddCatalogLookupPathPrefix(LOCALEDIR);
            #endif
            wxString prefix = wxStandardPaths::Get().GetInstallPrefix();
            locale.AddCatalogLookupPathPrefix(prefix);
        #endif
        locale.AddCatalogLookupPathPrefix(basepath);
        locale.AddCatalog(L"ts", (wxLanguage)locale.GetLanguage());
    }

    bool OnInit() {
        #if wxUSE_UNICODE == 0
        #error "must use unicode version of wx libs to ensure data integrity of .cts files"
        #endif
        ASSERT(wxUSE_UNICODE);

        #ifdef __WXMAC__
        wxDisableAsserts();
        //wxSystemOptions::SetOption("mac.toolbar.no-native", 1);
        #endif

        #ifdef __WXMSW__
            InitUnhandledExceptionFilter(argc, argv);

            // wxWidgets should really be doing this itself, but it doesn't (or expects you to
            // want to use a manifest), so we have to call it ourselves.

            // Attempt to load the different HiDPI API functions provided in User32 and Shcore

            #ifndef DPI_ENUMS_DECLARED
            typedef enum PROCESS_DPI_AWARENESS
            {
                PROCESS_DPI_UNAWARE = 0,
                PROCESS_SYSTEM_DPI_AWARE = 1,
                PROCESS_PER_MONITOR_DPI_AWARE = 2
            } PROCESS_DPI_AWARENESS;
            #endif

            typedef BOOL (WINAPI * SETPROCESSDPIAWARE_T)(void);
            typedef HRESULT (WINAPI * SETPROCESSDPIAWARENESS_T)(PROCESS_DPI_AWARENESS);
            typedef BOOL (WINAPI * SETPROCESSDPIAWARENESSCONTEXT_T)(DPI_AWARENESS_CONTEXT);
            SETPROCESSDPIAWARE_T SetProcessDPIAware = NULL;
            SETPROCESSDPIAWARENESS_T SetProcessDpiAwareness = NULL;
            SETPROCESSDPIAWARENESSCONTEXT_T SetProcessDpiAwarenessContext = NULL;
            HMODULE user32 = LoadLibraryA("User32.dll");
            HMODULE shcore = LoadLibraryA("Shcore.dll");
            if (user32) {
                SetProcessDPIAware =
                    (SETPROCESSDPIAWARE_T)GetProcAddress(user32, "SetProcessDPIAware");
                SetProcessDpiAwarenessContext =
                    (SETPROCESSDPIAWARENESSCONTEXT_T)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
            }
            if (shcore) {
                SetProcessDpiAwareness =
                    (SETPROCESSDPIAWARENESS_T)GetProcAddress(shcore, "SetProcessDpiAwareness");
            }

            // Call HiDPI API functions

            if (SetProcessDpiAwarenessContext) {
                SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            } else if (SetProcessDpiAwareness) {
                SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
            } else if (SetProcessDPIAware) {
                SetProcessDPIAware();
            }

            if (user32) FreeLibrary(user32);
            if (shcore) FreeLibrary(shcore);
        #endif

        auto lang = wxLocale::GetSystemLanguage();
        if (lang == wxLANGUAGE_UNKNOWN || !wxLocale::IsAvailable(lang)) {
            lang = wxLANGUAGE_ENGLISH;
        }
        locale.Init(lang);

        bool portable = false;
        bool single_instance = true;
        bool dump_builtins = false;
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') {
                switch ((int)argv[i][1]) {
                    case 'p': portable = true; break;
                    case 'i': single_instance = false; break;
                    case 'd':
                        dump_builtins = true;
                        single_instance = false;
                        break;
                }
            } else {
                filename = argv[i];
            }
        }

        if (single_instance) {
            instance_checker.reset(new wxSingleInstanceChecker(
                wxTheApp->GetAppName() + '-' + wxGetUserId(), wxStandardPaths::Get().GetTempDir()));
            if (instance_checker->IsAnotherRunning()) {
                wxClient client;
                client.MakeConnection(
                    L"localhost", L"4242",
                    filename.Len() ? filename.wc_str() : L"*");  // fire and forget
                return false;
            }
        }

        #if wxCHECK_VERSION(3, 1, 1)
            wxStandardPaths::Get().SetFileLayout(wxStandardPathsBase::FileLayout_XDG);
        #endif

        auto exepath = argv[0];
        #if defined(__WXMAC__)
            char path[PATH_MAX];
            uint32_t size = sizeof(path);
            if(_NSGetExecutablePath(path, &size) == 0) exepath = path;
        #elif defined(__WXGTK__)
            // argv[0] could be relative, this is apparently a more robust way to get the
            // full path.
            char path[PATH_MAX];
            auto len = readlink("/proc/self/exe", path, PATH_MAX - 1);
            if (len >= 0) {
                path[len] = 0;
                exepath = path;
            }
        #endif

        sys = new System(portable);
        frame = new MyFrame(exepath, this);

        auto serr = ScriptInit(frame->GetDataPath("scripts/"));
        if (!serr.empty()) {
            wxLogFatalError(L"Script system could not initialize: %s", serr);
            return false;
        }
        if (dump_builtins) {
            TSDumpBuiltinDoc();
            return false;
        }

        SetTopWindow(frame);

        serv->Create(L"4242");

        return true;
    }

    void OnEventLoopEnter(wxEventLoopBase *WXUNUSED(loop)) {
        if (!initateventloop) {
            initateventloop = true;
            frame->AppOnEventLoopEnter();
            sys->Init(filename);
        }
    }

    int OnExit() {
        DELETEP(sys);
        return 0;
    }

    void MacOpenFiles(const wxArrayString &fns) {
        if (!sys) return;
        for (auto &fn : fns) { sys->Open(fn); }
    }

    DECLARE_EVENT_TABLE()
};
