idf_component_register(SRCS "quiz_app.c" "login_app.c" "app_flow.c" "app_network.c" "main.cpp" "fronts/font_24_cn.c"
                    INCLUDE_DIRS "."
                    )

set_source_files_properties(fronts/font_24_cn.c PROPERTIES
    COMPILE_DEFINITIONS "static_bitmap=dsc"
)
