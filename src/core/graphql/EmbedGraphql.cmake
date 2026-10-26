# Build step (WP-04, D4): turns every src/core/graphql/*.graphql into a C++ string constant, so the app sends exactly the files
# that the contract test (tests/contract/) validates against the schema.
# Run as: cmake -DSRC_DIR=<folder with .graphql files> -DOUT=<generated .inc> -P EmbedGraphql.cmake
# Whole-line comments are left out of the strings (they'd only make every request bigger). One document per file; its name is
# the file name, which the contract test checks against the operation name.
cmake_minimum_required(VERSION 3.16)

file(GLOB docs "${SRC_DIR}/*.graphql")
list(SORT docs)
set(delim "snx_gql")
set(text "// Generated from src/core/graphql/*.graphql by src/core/graphql/EmbedGraphql.cmake. Do not edit.\n")
string(APPEND text "// Included only by src/core/graphql/Documents.cpp.\n\n")
set(table "")
foreach (doc ${docs})
    get_filename_component(name "${doc}" NAME_WE)
    if (NOT name MATCHES "^[A-Z][A-Za-z0-9]*$")
        message(FATAL_ERROR "${doc}: the file name must be the operation name in PascalCase (e.g. AboutServer.graphql)")
    endif ()
    file(READ "${doc}" body)
    string(FIND "${body}" ")${delim}\"" clash)
    if (NOT clash EQUAL -1)
        message(FATAL_ERROR "${doc} contains the text )${delim}\" and can't be embedded")
    endif ()
    # Drop whole-line comments, then blank lines.
    string(REGEX REPLACE "(^|\n)[ \t]*#[^\n]*" "\\1" body "${body}")
    string(REGEX REPLACE "\n[ \t]*\n+" "\n" body "${body}")
    string(REGEX REPLACE "^[ \t\n]+" "" body "${body}")
    string(APPEND text "constexpr std::string_view k${name} = R\"${delim}(${body})${delim}\";\n")
    string(APPEND table "    {\"${name}\", k${name}},\n")
endforeach ()
string(APPEND text "\nconstexpr GqlDocument kAllDocuments[] = {\n${table}};\n")

# Write only when something changed, so an unchanged document doesn't rebuild anything.
file(WRITE "${OUT}.tmp" "${text}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUT}.tmp" "${OUT}")
file(REMOVE "${OUT}.tmp")
