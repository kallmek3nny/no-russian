cmake_minimum_required(VERSION 3.20)
project(ThaumtMourge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# MinGW: link the C++ runtime statically so tests don't load a mismatched
# libstdc++-6.dll from PATH (STATUS_ENTRYPOINT_NOT_FOUND / 0xc0000139).
if(MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# Legend Engine T - Milestone A foundation modules
add_library(TCore INTERFACE)
target_include_directories(TCore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/TCore/include)

add_library(TWorld INTERFACE)
target_include_directories(TWorld INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/TWorld/include)
target_link_libraries(TWorld INTERFACE TCore)

add_executable(test_foundation tests/test_foundation.cpp)
target_link_libraries(test_foundation PRIVATE TWorld)
add_test(NAME foundation COMMAND test_foundation)

add_executable(test_journal tests/test_journal.cpp)
target_link_libraries(test_journal PRIVATE TWorld)
add_test(NAME journal COMMAND test_journal)

add_library(TEntity INTERFACE)
target_include_directories(TEntity INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/TEntity/include)
target_link_libraries(TEntity INTERFACE TCore)

add_executable(test_ecs tests/test_ecs.cpp)
target_link_libraries(test_ecs PRIVATE TEntity)
add_test(NAME ecs COMMAND test_ecs)

add_executable(test_input tests/test_input.cpp)
target_link_libraries(test_input PRIVATE TCore)
add_test(NAME input COMMAND test_input)

add_executable(test_crashlog tests/test_crashlog.cpp)
target_link_libraries(test_crashlog PRIVATE TCore)
add_test(NAME crashlog COMMAND test_crashlog)

add_executable(test_event tests/test_event.cpp)
target_link_libraries(test_event PRIVATE TWorld)
add_test(NAME event COMMAND test_event)

add_executable(test_research tests/test_research.cpp)
target_link_libraries(test_research PRIVATE TWorld)
add_test(NAME research COMMAND test_research)

add_library(TCombat INTERFACE)
target_include_directories(TCombat INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/TCombat/include)
target_link_libraries(TCombat INTERFACE TCore)

add_executable(test_weapon tests/test_weapon.cpp)
target_link_libraries(test_weapon PRIVATE TCombat TWorld)
add_test(NAME weapon COMMAND test_weapon)

add_executable(test_boss tests/test_boss.cpp)
target_link_libraries(test_boss PRIVATE TCombat TWorld)
add_test(NAME boss COMMAND test_boss)

add_executable(test_biome tests/test_biome.cpp)
target_link_libraries(test_biome PRIVATE TWorld)
add_test(NAME biome COMMAND test_biome)

add_executable(test_dungeon tests/test_dungeon.cpp)
target_link_libraries(test_dungeon PRIVATE TWorld)
add_test(NAME dungeon COMMAND test_dungeon)

add_executable(test_city tests/test_city.cpp)
target_link_libraries(test_city PRIVATE TWorld)
add_test(NAME city COMMAND test_city)

add_executable(test_save tests/test_save.cpp)
target_link_libraries(test_save PRIVATE TCore)
add_test(NAME save COMMAND test_save)

add_executable(test_serialize tests/test_serialize.cpp)
target_link_libraries(test_serialize PRIVATE TCore)
add_test(NAME serialize COMMAND test_serialize)

add_executable(test_enemy tests/test_enemy.cpp)
target_link_libraries(test_enemy PRIVATE TCombat TWorld)
add_test(NAME enemy COMMAND test_enemy)

add_library(TAnim INTERFACE)
target_include_directories(TAnim INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/TAnim/include)
target_link_libraries(TAnim INTERFACE TCore)

add_executable(test_anim tests/test_anim.cpp)
target_link_libraries(test_anim PRIVATE TAnim)
add_test(NAME anim COMMAND test_anim)

add_library(TPhysics INTERFACE)
target_include_directories(TPhysics INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/TPhysics/include)
target_link_libraries(TPhysics INTERFACE TWorld)

add_executable(test_physics tests/test_physics.cpp)
target_link_libraries(test_physics PRIVATE TPhysics)
add_test(NAME physics COMMAND test_physics)

# Milestone B game layer
add_library(Game INTERFACE)
target_include_directories(Game INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/game/include)
target_link_libraries(Game INTERFACE TPhysics TCore)

add_executable(test_game tests/test_game.cpp)
target_link_libraries(test_game PRIVATE Game)
add_test(NAME game COMMAND test_game)

# Section 26 localisation
add_executable(test_localization tests/test_localization.cpp)
target_link_libraries(test_localization PRIVATE TCore)
target_compile_definitions(test_localization PRIVATE LT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME localization COMMAND test_localization)

# Section 12 equipment and inventory
add_executable(test_equipment tests/test_equipment.cpp)
target_link_libraries(test_equipment PRIVATE Game)
add_test(NAME equipment COMMAND test_equipment)

# Sections 21/25/26 JSON data records
add_executable(test_json tests/test_json.cpp)
target_link_libraries(test_json PRIVATE TCore)
target_compile_definitions(test_json PRIVATE LT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# MSVC's traditional preprocessor does not escape backslashes or quotes when it stringizes
# raw string literals passed to the CHECK macros (C2017/C3850/C2153); the conforming
# preprocessor does, so test_json opts into it.
if(MSVC)
  target_compile_options(test_json PRIVATE /Zc:preprocessor)
endif()
add_test(NAME json COMMAND test_json)

# Sections 6/11 starting classes, progression tracks, and faction reputation
add_executable(test_progression tests/test_progression.cpp)
target_link_libraries(test_progression PRIVATE Game)
add_test(NAME progression COMMAND test_progression)

# Section 11 NPCs: schedules, relationship graph, fates and phantom records
add_executable(test_npc tests/test_npc.cpp)
target_link_libraries(test_npc PRIVATE Game)
add_test(NAME npc COMMAND test_npc)

# Sections 15/23/24 TAudio: attack-class combat cues, cue shapes, biome music stems
add_library(TAudio INTERFACE)
target_include_directories(TAudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/TAudio/include)
target_link_libraries(TAudio INTERFACE TAnim)

add_executable(test_audio tests/test_audio.cpp)
target_link_libraries(test_audio PRIVATE TAudio)
add_test(NAME audio COMMAND test_audio)

# Section 3 narrative: acts, routes, city outcomes, Council fates, Crown Keys, endings
add_executable(test_narrative tests/test_narrative.cpp)
target_link_libraries(test_narrative PRIVATE Game)
add_test(NAME narrative COMMAND test_narrative)

# Sections 13/14/15 TNet: co-op session, snapshot interpolation, region interest,
# rollback-safe combat events, matchmaking adapter and the asynchronous co-op ledger
add_library(TNet INTERFACE)
target_include_directories(TNet INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/TNet/include)
target_link_libraries(TNet INTERFACE TCore)

add_executable(test_net tests/test_net.cpp)
target_link_libraries(test_net PRIVATE TNet)
add_test(NAME net COMMAND test_net)

# Sections 15/19/23 TRender: capability tiers, per-tier features, Android C31 budget
add_library(TRender INTERFACE)
target_include_directories(TRender INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/TRender/include)
target_link_libraries(TRender INTERFACE TCore)

add_executable(test_render tests/test_render.cpp)
target_link_libraries(test_render PRIVATE TRender)
add_test(NAME render COMMAND test_render)

# Sections 15/23 TTools: per-stage frame profiler, frame budgets, 30-minute traversal capture
add_library(TTools INTERFACE)
target_include_directories(TTools INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/TTools/include)
target_link_libraries(TTools INTERFACE TCore)

add_executable(test_profiler tests/test_profiler.cpp)
target_link_libraries(test_profiler PRIVATE TTools)
add_test(NAME profiler COMMAND test_profiler)

# Sections 14/15/19 TTools: save inspector, rolling-backup chain report, settings-menu save recovery
add_executable(test_save_inspector tests/test_save_inspector.cpp)
target_link_libraries(test_save_inspector PRIVATE TTools)
add_test(NAME save_inspector COMMAND test_save_inspector)

# Sections 4.4/15/25 TTools: schematic validator, nav baker and prefab converter
add_executable(test_prefab tests/test_prefab.cpp)
target_link_libraries(test_prefab PRIVATE TTools TWorld)
add_test(NAME prefab COMMAND test_prefab)

# Sections 15/16/23 TLight: per-tier lighting passes (HDR, bloom, volumetric fog, god rays,
# probes, planar reflection) derived from TRender tiers, plus the emissive colour language
add_library(TLight INTERFACE)
target_include_directories(TLight INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/TLight/include)
target_link_libraries(TLight INTERFACE TRender)

add_executable(test_lighting tests/test_lighting.cpp)
target_link_libraries(test_lighting PRIVATE TLight)
add_test(NAME lighting COMMAND test_lighting)

# Section 25 TTools: content validator CI gate (duplicate/unknown ids, missing loc keys,
# recipe cycles, oversized textures, save incompatibilities, event deadlocks)
add_executable(test_content_validator tests/test_content_validator.cpp)
target_link_libraries(test_content_validator PRIVATE TTools)
add_test(NAME content_validator COMMAND test_content_validator)

# Sections 14/19/25 TCore: mod manifests (mods/<id>/mod.json), SHA-256 / HMAC-SHA256, and the
# plugin policy (Android code plugins off by default, signed permission manifests pinned to the library hash)
add_executable(test_mod_manifest tests/test_mod_manifest.cpp)
target_link_libraries(test_mod_manifest PRIVATE TCore)
add_test(NAME mod_manifest COMMAND test_mod_manifest)

# Sections 14/17/22 game profile layer: platform settings, accessibility, touch layout prefs,
# research unlocks, and profile.json persistence inside the checksummed save envelope
add_executable(test_profile tests/test_profile.cpp)
target_link_libraries(test_profile PRIVATE Game TRender)
add_test(NAME profile COMMAND test_profile)

# Section 8 crafting loop: device gating, recipe unlocks, aspect costs, flux risk, failed-infusion
# outcomes (mutation, hostile construct, unique item) with aspect salvage, and Thaumonomicon records
add_executable(test_crafting tests/test_crafting.cpp)
target_link_libraries(test_crafting PRIVATE Game)
add_test(NAME crafting COMMAND test_crafting)

# Sections 2.5/4.3/6/26 character creation: sheet validation, UTF-8 (Cyrillic) names, scars,
# starting city ids, and the memory-shrine origin reveal that shifts faction judgement
add_executable(test_character tests/test_character.cpp)
target_link_libraries(test_character PRIVATE Game)
add_test(NAME character COMMAND test_character)

# Sections 17/22/26 UI model: main menu order, enable rules and focus navigation; HUD meters, compass,
# visibility, optional damage numbers, dp layout checked against the touch controls, and en/ru UI loc keys
add_executable(test_ui tests/test_ui.cpp)
target_link_libraries(test_ui PRIVATE Game TRender TTools)
target_compile_definitions(test_ui PRIVATE LT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME ui COMMAND test_ui)

# Section 5 world population: deterministic region population/story budgets and the
# season/landmark/history-aware event filler that seeds rumours into the EventLedger
add_executable(test_population tests/test_population.cpp)
target_link_libraries(test_population PRIVATE TWorld)
add_test(NAME population COMMAND test_population)

# Sections 15/19/23 TCore memory: linear scratch arenas with rewindable markers, nested scopes,
# non-throwing exhaustion and peak tracking for per-frame scratch budgets
add_executable(test_memory tests/test_memory.cpp)
target_link_libraries(test_memory PRIVATE TCore)
add_test(NAME memory COMMAND test_memory)

# Sections 15/19/23 TCore jobs: fixed worker pool over a FIFO queue, counter waits that help run
# queued jobs, exception accounting, nested submission, and results independent of worker count
find_package(Threads REQUIRED)
add_executable(test_jobs tests/test_jobs.cpp)
target_link_libraries(test_jobs PRIVATE TCore Threads::Threads)
add_test(NAME jobs COMMAND test_jobs)

# Sections 21/25 TCombat weapon records: strict decoding of data/items/*.json and mods/<id>/items.json
# into WeaponDefs, archetype timing fallback, per-record errors, and the shipped vertical-slice items
add_executable(test_weapon_record tests/test_weapon_record.cpp)
target_link_libraries(test_weapon_record PRIVATE TCombat)
target_compile_definitions(test_weapon_record PRIVATE LT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# Raw string literals passed to the CHECK macros need MSVC's conforming preprocessor (see test_json).
if(MSVC)
  target_compile_options(test_weapon_record PRIVATE /Zc:preprocessor)
endif()
add_test(NAME weapon_record COMMAND test_weapon_record)

# Sections 21/25 TCombat enemy records: strict decoding of data/enemies/*.json and mods/<id>/enemies.json
# into EnemyDefs, optional-key defaults, per-record errors, and the shipped common families
add_executable(test_enemy_record tests/test_enemy_record.cpp)
target_link_libraries(test_enemy_record PRIVATE TCombat)
target_compile_definitions(test_enemy_record PRIVATE LT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# Raw string literals passed to the CHECK macros need MSVC's conforming preprocessor (see test_json).
if(MSVC)
  target_compile_options(test_enemy_record PRIVATE /Zc:preprocessor)
endif()
add_test(NAME enemy_record COMMAND test_enemy_record)

# Sections 5/21/25 TWorld event records: strict decoding of data/events/*.json and mods/<id>/events.json
# into EventDefs, rumour-to-aftermath stage checks, per-record errors, and the shipped autonomous events
add_executable(test_event_record tests/test_event_record.cpp)
target_link_libraries(test_event_record PRIVATE TWorld)
target_compile_definitions(test_event_record PRIVATE LT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# Raw string literals passed to the CHECK macros need MSVC's conforming preprocessor (see test_json).
if(MSVC)
  target_compile_options(test_event_record PRIVATE /Zc:preprocessor)
endif()
add_test(NAME event_record COMMAND test_event_record)

# Sections 21/25 TTools record bridge: decoded weapon, enemy and event records from data/ and every
# mods/<id>/ merged in a stable order into the content validator, with per-file load errors
add_executable(test_record_content tests/test_record_content.cpp)
target_link_libraries(test_record_content PRIVATE TTools TCombat TWorld)
target_compile_definitions(test_record_content PRIVATE LT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME record_content COMMAND test_record_content)

# Sections 12/21/25 TCombat armour records: strict decoding of data/armor/*.json and mods/<id>/armor.json
# into ArmourDefs, neutral default resistances, slot-name round-trips, and per-record errors
add_executable(test_armour_record tests/test_armour_record.cpp)
target_link_libraries(test_armour_record PRIVATE TCombat)
# Raw string literals passed to the CHECK macros need MSVC's conforming preprocessor (see test_json).
if(MSVC)
  target_compile_options(test_armour_record PRIVATE /Zc:preprocessor)
endif()
add_test(NAME armour_record COMMAND test_armour_record)

# Sections 10/14/21/25 TCombat boss records: strict decoding of data/bosses/*.json and mods/<id>/bosses.json
# into BossDefs that drive BossState, ordered phase thresholds, armour gates, and per-record errors
add_executable(test_boss_record tests/test_boss_record.cpp)
target_link_libraries(test_boss_record PRIVATE TCombat)
# Raw string literals passed to the CHECK macros need MSVC's conforming preprocessor (see test_json).
if(MSVC)
  target_compile_options(test_boss_record PRIVATE /Zc:preprocessor)
endif()
add_test(NAME boss_record COMMAND test_boss_record)

# Sections 4.2/14/21/25 TWorld biome records: strict decoding of data/biomes/*.json into BiomeDefs equal
# to frostCrown(), identical generated chunks, block-name round-trips, and per-record errors
add_executable(test_biome_record tests/test_biome_record.cpp)
target_link_libraries(test_biome_record PRIVATE TWorld)
target_compile_definitions(test_biome_record PRIVATE LT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME biome_record COMMAND test_biome_record)

# Sections 8/14/21/25 TWorld infusion recipe records: strict decoding of data/recipes/*.json into RecipeDefs
# matching the crafting loop's InfusionRecipe defaults, the shipped aether core recipe, and per-record errors
add_executable(test_recipe_record tests/test_recipe_record.cpp)
target_link_libraries(test_recipe_record PRIVATE TWorld)
target_compile_definitions(test_recipe_record PRIVATE LT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME recipe_record COMMAND test_recipe_record)

# Sections 7/12/21/25 TCombat attachment records: strict decoding of data/attachments/*.json into AttachmentDefs,
# neutral omitted modifiers, decoded modifiers feeding installAttachments, the shipped core set, and per-record errors
add_executable(test_attachment_record tests/test_attachment_record.cpp)
target_link_libraries(test_attachment_record PRIVATE TCombat)
target_compile_definitions(test_attachment_record PRIVATE LT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME attachment_record COMMAND test_attachment_record)
