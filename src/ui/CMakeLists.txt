# ui: the screens' models (src/ui/<screen>/<Name>Model.*, src/ui/README.md). They hold each screen's state and rules without
# borealis, so the host unit tests build them too. The views (*Activity, *Tab, components) need borealis and are compiled
# into the app itself (src/app/CMakeLists.txt).
add_library(snx_ui_models STATIC
    components/GridLayout.cpp
    library/LibraryModel.cpp
    manga/MangaModel.cpp
    login/LoginModel.cpp
    main/MainModel.cpp
    setup/SetupModel.cpp
)
target_link_libraries(snx_ui_models PUBLIC snx_api)
snx_target_defaults(snx_ui_models)
