# SPDX-License-Identifier: AGPL-3.0-only
#
# Library: SQLite database, migrations, background scanner, and metadata.
zyron_require_sqlite3()

add_library(zyron_library STATIC
  Database/Database.cpp
  Database/DatabaseWriter.cpp
  Database/LibraryRepository.cpp
  Database/Migrations.cpp
  Hash/ContentHasher.cpp
  Metadata/BuiltinMetadataReader.cpp
  Metadata/MetadataExtractor.cpp
  Scanner/LibraryScanner.cpp
  Analysis/AnalysisTaskQueue.cpp
  Database/SqliteLibrarySource.cpp
  LibraryService.cpp)
add_library(zyron::library ALIAS zyron_library)

target_include_directories(zyron_library PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(zyron_library PUBLIC zyron::core zyron::sqlite3)
zyron_target_defaults(zyron_library)
