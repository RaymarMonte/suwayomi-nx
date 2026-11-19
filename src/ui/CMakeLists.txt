# ui: the screens' models (src/ui/<screen>/<Name>Model.*, src/ui/README.md). They hold each screen's state and rules without
# borealis, so the host unit tests build them too. The views (*Activity, *Tab, components) need borealis and are compiled
# into the app itself (src/app/CMakeLists.txt).
add_library(snx_ui_models STATIC
    components/GridLayout.cpp
    browse/GlobalSearchModel.cpp
    browse/LanguagesModel.cpp
    browse/LibraryAdder.cpp
    browse/SourceBrowseModel.cpp
    browse/SourceFiltersModel.cpp
    browse/SourcesModel.cpp
    categories/CategoriesModel.cpp
    categories/CategoryPickerModel.cpp
    library/LibraryModel.cpp
    manga/MangaModel.cpp
    reader/ChapterPages.cpp
    reader/ReaderModel.cpp
    reader/ProgressSaver.cpp
    reader/StripModel.cpp
    login/LoginModel.cpp
    main/MainModel.cpp
    updates/ChapterRowsModel.cpp
    updates/LibraryUpdater.cpp
    downloads/DownloadStore.cpp
    downloads/ServerDownloads.cpp
    downloads/SwitchDownloads.cpp
    reader/WaitingSaves.cpp
    setup/SetupModel.cpp
)
target_link_libraries(snx_ui_models PUBLIC snx_api snx_image)
snx_target_defaults(snx_ui_models)
