# THAUMT MOURGE
## Full Game Design Document
### Legend Engine T | RDGaming Studios | Windows / Linux / Android
### Production draft 1.0 — English master document

> **Working rule:** this document is the source of truth for the agent team. Every implementation task must identify the systems, data contracts, tests, and platform impact it touches. The game is an original dark fantasy action RPG using voxel exploration and thaumaturgical research as its pillars. Minecraft, Sekiro, Elden Ring, and Thaumcraft are references for categories of play, not assets or copied content.

---

## 1. Product definition

**ThaumtMourge** is a third-person open-world voxel action RPG about a survivor recruited into the Order of Thaumiel during the collapse of the Taumaurg Council. The player explores a world whose terrain, ruins, dungeons, weather, settlements, and supernatural events are generated from a persistent seed. The player studies the six primal aspects of Taumium, uses them to modify weapons and armour, and decides whether the Order should preserve, weaponise, or destroy the technology.

The target experience is an indie-scale “Elden Ring with a voxel frontier”: deliberate melee, readable enemy attacks, dangerous discovery, interconnected landmarks, build freedom, environmental destruction, and lore told through places and objects. It must run as one data-driven project on Windows, Linux, and Android, with platform-specific renderer and input layers over the same simulation core.

**Primary pillars**

1. **A world that keeps producing stories.** Every region has authored constraints and a deterministic seed, then fills its empty spaces with events, ruins, expeditions, hunts, anomalies, and travelling factions.
2. **Skill-first combat.** Guard, perfect deflection, posture breaks, evasive movement, spacing, status effects, spells, ranged tools, and destructible cover all matter.
3. **Research as progression.** The player discovers aspects by scanning creatures, blocks, weather, relics, and failures. Research unlocks technology rather than simply granting a level.
4. **Build anywhere, consequence everywhere.** Mining, placement, destruction, weathering, fire, flux contamination, and settlement support are persistent.
5. **A strong visual identity.** Pixel-art-informed palettes, hard silhouettes, luminous Taumium, chunky voxel forms, fog layers, animated grass, and dark cyan/amber magic communicate the game at low and high resolutions.

**Audience and rating target**

Single-player first, optional cooperative play; dark fantasy violence, no graphic gore requirement. The game should be readable on a 6-inch 720p Android display and scale to 1440p/4K desktop displays.

---

## 2. Canon and world history

### 2.1 The First Spark

Before recorded history, the planet was threaded by the **Aureline**, a planetary lattice of natural energy. The Ancients learned to condense Aureline into a programmable crystal-metal: **Taumium**. Taumium is not “mana”; it is a memory-bearing material capable of storing patterns, intent, and changes to local physical rules.

The Ancients built the first **aura nodes**, towers that stabilised weather, soil, migration, and portals. Their civilisation ended when the Council tried to compress every node into one perfect world-mind. The resulting disaster, the **Mourge**, fractured the lattice and seeded the surface with dead zones, taint, floating ruins, and unstable time pockets.

### 2.2 The Order of Thaumiel

The Order of Thaumiel formed from wardens, researchers, masons, and soldiers who guarded the surviving nodes. Their oath is: **“Measure the wonder. Restrain the hunger.”** The Order contains four internal doctrines:

- **The Sealkeepers:** preserve nodes and restrict dangerous research.
- **The Infusers:** turn Taumium into weapons, armour, constructs, and engines.
- **The Field Choir:** travel, scan, map, and repair the world.
- **The Ash Tribunal:** destroy research they believe cannot be controlled.

### 2.3 The Tautmaurg Council

The Council of Tautmaurgs was created as a scientific and political body above the Order. Each Tautmaurg controlled one aspect discipline. The Council intended to reopen the **Crown Gate**, the central Ancient machine, and end the world’s instability. Instead, the ritual pulled the Mourge forward. The Council broke into living bodies, memory-ghosts, and regional tyrants.

The player begins after this event. The Order is fractured, cities are isolated, and the Council’s surviving members each claim to be the only person capable of saving the world.

### 2.4 The Mourge

The Mourge is a planetary wound, not a single monster. It appears as black snow, reversed waterfalls, sky seams, whispering grass, hostile reflections, and regions where the player’s previous actions repeat as hostile echoes. Flux is the measurable pollution produced when Taumium is forced to do contradictory things.

### 2.5 The player character

The player is a **Mourge-marked survivor** recovered from a sealed transit vault. Their body contains a dormant Taumium “aura key”, allowing them to read nodes and survive short exposure to flux. The player’s identity is customisable; their forgotten past is revealed through optional memory shrines and changes how some factions judge them.

The player is not a chosen god. They are a capable, vulnerable person whose decisions determine which systems survive.

---

## 3. Narrative structure

### Act I — The Broken Seal

The player reaches the Order’s ruined frontier citadel, chooses a starting city connection, and learns the three immediate threats: node starvation, Tautmaurg warbands, and a growing tainted grove. The first major objective is to relight three local aura nodes.

### Act II — The Five Roads

The world opens into five regional routes. Each route introduces one aspect, a faction conflict, a city questline, a traversal mechanic, and a Council memory. The player can approach routes in different orders; boss resistances and NPC dialogue reflect prior choices.

### Act III — The Council War

The surviving Tautmaurgs openly wage war. Cities can be defended, abandoned, or placed under a faction’s protection. Dynamic events can change road safety, vendors, enemy patrols, and available dungeons.

### Act IV — The Crown Gate

The player assembles or destroys the five **Crown Keys**. The final region is a shifting Ancient megastructure whose layout is stable for a seed but changes state based on which Council members were killed, redeemed, or empowered.

### Endings

- **The Seal:** stabilise the Aureline, sacrificing most advanced Taumium technology.
- **The Throne:** merge with the Crown Gate and become a new regulator; the world survives under central control.
- **The Wild Equation:** release the nodes and accept a permanently changing world.
- **The Ash Verdict:** destroy Taumium and end all portals, leaving a harsh but human future.
- **The Mourge:** fail or deliberately feed the gate; unlock an altered post-game world.

---

## 4. World structure

### 4.1 Scale

The canonical world coordinate space is approximately **35,000 × 35,000 km**. It is not stored as a full array. Legend Engine T uses a 64-bit world coordinate, a floating origin around the player, deterministic chunk generation, and an edit journal. Only nearby chunks, event anchors, and persistent edits are loaded.

- Voxel cell: 1 metre baseline; important dungeons may use 0.5 m detail cells.
- Chunk: 32 × 32 × 128 cells for desktop; mobile mesh simplification can use 32 × 32 × 96.
- Region: 16 × 16 chunks, with a region manifest and event seed.
- Streaming ring: configurable 5–12 chunks on Android, 12–32 on desktop.
- Persistent edit format: append-only block/entity/event journal with periodic compaction.
- Floating origin: rebase every 512 metres or at a safe streaming boundary.

### 4.2 Biomes

1. **Verdant Reach:** meadows, oak-like groves, old roads, beginner ruins.
2. **Salt Dune:** dunes, buried observatories, mirage enemies, heat storms.
3. **Mire of Glass:** wetlands, reflective water, poison reeds, drowned libraries.
4. **Frost Crown:** frozen valleys, blue aurora, ice caves, frostbound knights.
5. **Tainted Grove:** warped trees, flux flowers, hostile familiar spirits.
6. **Aether Highlands:** cliffs, floating islands, wind bridges, sky predators.
7. **Ash Barrens:** volcanic shelves, ruined foundries, fire and smoke weather.
8. **Underroot:** deep caves, fungal forests, Ancient rails, underground cities.

Each biome has: surface palette, terrain generator, ore table, vegetation grammar, weather set, ambient audio palette, ruin kit, enemy family table, event table, and boss modifiers.

### 4.3 Cities

- **Aurelia:** Order capital, research tables, training courtyard, Sealkeeper politics.
- **Mournhollow:** cemetery city built around a Mourge fracture; memory economy.
- **Veyr Harbor:** sea trade, smugglers, boats, fishing, storm contracts.
- **Khar-Duun:** industrial forge city, handcannons, golems, mining guilds.
- **The Verdant Court:** druidic city grown through living trees; beast alliances.

Cities are authored hubs with modular streets and buildings. Their surrounding districts are seeded, allowing unique roads, farms, mines, shrines, and regional events.

### 4.4 Dungeon families

- Ancient laboratories: rotating machines, research puzzles, portal chambers.
- Order bastions: shield formations, locked archives, duel arenas.
- Mourge fractures: looping rooms, memory echoes, reversed gravity.
- Drowned libraries: water pressure, mirror doors, ink creatures.
- Buried observatories: star maps, sand collapse, astronomical bosses.
- Tautmaurg citadels: faction-specific mechanics and a Council boss.
- Underroot rail vaults: mine carts, darkness, fungal ecosystems.

Hand-authored dungeon rooms are stored as engine-native prefabs. Schematic-like import is supported only through a converter that validates block IDs, collision, nav data, lighting probes, and platform budgets.

---

## 5. World population and autonomous events

Every region receives a **population budget** and a **story budget**. The filler system never places random noise without purpose; it selects an event compatible with biome, faction, season, recent player history, and nearby landmarks.

**Event families**

- Caravan under siege.
- Tautmaurg duel with a choice to intervene.
- Riftfall spawning a temporary dungeon.
- Lost research party requesting escort.
- Wyvern nest and egg theft.
- Golem uprising in a mining settlement.
- Tainted bloom spreading from a flux node.
- Ash storm revealing a buried vault.
- Frost warden hunt.
- Bandit siege changing a road checkpoint.
- Shrine eclipse enabling a rare upgrade.
- Travelling merchant with one seed-exclusive item.
- NPC expedition that can succeed without the player.
- Asynchronous phantom replay from another player.

Events have lifecycle states: **rumour → visible → active → resolved → aftermath**. The aftermath persists and can create a new vendor, ruin, grave, faction patrol, shortcut, or hostile remnant.

---

## 6. Player, classes, and progression

Character creation includes name, body silhouette, voice set, origin memory, face palette, scars, cloak, starting city connection, and class. Class is a starting toolkit, not a permanent ability prison.

**Starting classes**

- **Warden:** shield, deflection, armour, posture damage.
- **Spellblade:** weapon infusion, short-range arcane attacks.
- **Thaumaturge:** aspects, research speed, flux control, ranged spells.
- **Ranger:** bow, traps, mobility, beast knowledge.
- **Ashen Monk:** unarmed chains, perfect timing, fire resistance.
- **Golemancer:** construct commands, turrets, resource automation.

Progression has four parallel tracks:

1. **Rank:** health, stamina, focus, equipment capacity.
2. **Research:** aspects, recipes, devices, dungeon knowledge.
3. **Masteries:** weapon movesets and defensive timing.
4. **Reputation:** faction access, prices, patrol behaviour, endings.

No class owns an exclusive endgame build. Any player may learn any weapon school or aspect, with different research costs and training quests.

---

## 7. Combat design

Combat is third-person, lock-on optional, with animation commitment and readable telegraphs.

**Core resources**

- Health: damage and healing.
- Stamina: sprint, dodge, heavy attacks, guard stability.
- Posture: breaks under attacks, perfect deflections, and aspect counters.
- Vis: spell and weapon infusion fuel.
- Flux: dangerous pollution; high flux changes enemies and terrain.

**Defence**

- Guard reduces damage but consumes stamina.
- Perfect deflection occurs in a short timing window and damages enemy posture.
- Dodge has directional invulnerability, with a heavier armour recovery penalty.
- Jump, crouch, climbing, and terrain height matter for attacks.

**Weapon families**

Greatblades, longswords, spears, axes, hammers, sickles, bows, crossbows, handcannons, arcane foci, chakrams, gauntlets, shields, and improvised voxel tools. Every family has light, heavy, sprint, charged, air, guard-counter, and execution moves.

**Weapon attachments**

Optics, runes, cores, grips, magazines, guards, muzzles, back-mounted capacitors, and planar lenses change damage, posture, range, noise, elemental response, and flux generation. Attachments have compatibility tags and can be physically visible on the model.

**Damage channels**

Physical, frost, ash, verdant, aether, void, lightning, poison, and flux. Resistances are contextual: wet enemies conduct lightning; tainted enemies may heal from void; frost armour can crack under ash.

**Destructibility**

Walls, cover, trees, bridges, doors, and selected dungeon machinery have material health. Destruction creates nav changes, debris, dust, and event consequences. The player cannot flatten the entire world; structural anchors protect critical authored spaces.

---

## 8. Thaumium, research, and crafting

The six primal aspects are **Motion, Form, Life, Decay, Light, and Memory**. Hybrid aspects emerge from combinations. Research is discovered through scanning and experimentation, then solved through a node-link research interface.

**Devices**

Research table, infusion altar, flux condenser, aura extractor, golem workbench, planar lens, node stabiliser, teleport beacon, weather engine, and mobile field kit.

**Crafting loops**

- Scan a material or creature.
- Extract aspects and refine Taumium.
- Risk flux during infusion.
- Install a weapon/armour attachment.
- Test it in the field.
- Record the result in the Thaumonomicon.

Failed infusions can produce temporary mutations, hostile constructs, or unique items. Failure is content, not a reload tax.

---

## 9. Enemies

Enemy AI uses perception, intent, posture, navigation, group roles, and event state.

**Common families**

- Mourge husks: basic melee and memory echoes.
- Glass leeches: wetland ambushers and mirror clones.
- Salt wraiths: ranged mirage casters.
- Frostbound knights: shields, counters, ice zones.
- Tainted familiars: small fast creatures that alter aura.
- Ash crawlers: burrowers with fire vents.
- Golem miners: neutral until resources are stolen.
- Order deserters: human weapon users with deflection skills.
- Wyverns: ground movement, short flight, fire spit, area orb.
- Void moths: swarm and light-drain behaviour.
- Root titans: environmental bosses and moving cover.

Enemy variants are generated by **aspect mutation**, **faction kit**, **biome adaptation**, and **event scars**, allowing deterministic but varied encounters.

---

## 10. Major bosses

### The First Tautmaurg — “The Man Who Measured God”
A former Council chair in a rotating laboratory arena. He changes the arena’s gravity and copies the player’s most-used attack. Phase two removes his body and leaves a hostile research pattern.

### Veyra, Glass Saint
Mire boss who fights through reflections. The player must identify the real body by sound, ripples, and aspect residue.

### The Frost Warden
A knight whose armour is a frozen fortress. Breaking pieces changes his moveset and opens alternate routes through the arena.

### Khar-Duun’s Furnace Mother
A giant industrial golem that uses conveyor belts, molten channels, detachable arms, and worker constructs. The player may repair or sabotage parts of the factory.

### The Wyvern of the Ninth Nest
A rare aerial hunt. Its weakness is a comedic but canonical Taumium food debuff: certain fermented “macarons” and weapon grease disrupt its fire glands. The weakness is discovered through research, not assumed.

### The Tautmaurg of Memory
An optional boss that replays the player’s previous deaths, builds, and choices. In co-op, it can use recorded phantom behaviour from both players.

### The Crown Gate
Final multi-stage encounter against a machine, a world wound, and the player’s chosen ending. The arena rearranges authored chunks based on the five keys.

---

## 11. NPCs and factions

Every important NPC has a goal, fear, relationship graph, faction, schedule, combat role, and failure state. NPCs can die, move, become vendors, become enemies, or leave a phantom record.

**Key characters**

- **Magister Ilyra Voss:** Sealkeeper mentor; wants controlled restoration.
- **Marshal Orun Vale:** war leader; believes the Council can only be stopped by force.
- **Sister Neme:** Field Choir cartographer; unlocks regional event maps.
- **Rusk of Khar-Duun:** golem engineer; turns destroyed machines into companions.
- **The Nameless Archivist:** remembers every ending but cannot act directly.
- **Veyra before the glass:** optional pre-boss relationship quest.

Factions include the Order of Thaumiel, Tautmaurg remnants, Khar-Duun miners, Veyr smugglers, Verdant Court, Ash Tribunal, free caravans, and Mourge cults.

---

## 12. Equipment and inventory

Equipment slots: head, torso, hands, legs, cloak, main hand, off hand, ranged, focus, four attachment slots, relic, and quick-use belt. Weight influences poise, stamina recovery, noise, and dodge animation.

Items have rarity only as a secondary label. A low-rarity item with a unique research trait can remain useful. Every generated item stores seed, provenance, aspect tags, durability, sockets, and history.

---

## 13. Multiplayer and asynchronous systems

Co-op supports 2–4 players in a host-authoritative session. The simulation uses deterministic seeds, snapshot interpolation, interest management by region, and rollback-safe combat events.

**Asynchronous features**

- Ghosts showing other players’ deaths and routes.
- Messages and warnings placed as world runes.
- Shared discoveries and discovered aspect fragments.
- Player-built beacons appearing in another player’s world as limited echoes.
- Caravan and settlement outcomes aggregated as world events.
- Optional invasion/duel contracts in marked arenas.

The game remains fully playable offline. Matchmaking is a separate service and never owns the single-player save.

---

## 14. Save system and data layout

Game saves are separate from binaries and use versioned, checksummed files.

```text
ThaumtMourge/
  bin/
    windows-x64/thaumtmourge.exe
    linux-x64/thaumtmourge
    android-arm64/libthaumtmourge.so
  data/
    boot.lgt
    biomes/*.json
    items/*.json
    enemies/*.json
    bosses/*.json
    dungeons/*.prefab
    textures/*.ktx2
    audio/*.opus
    shaders/*.glsl
  saves/
    profile.json
    world_<seed>.tmw
    journal_<region>.tmj
    screenshots/
  mods/
```

Save layers:

1. Profile: settings, accessibility, unlocked research.
2. Character: stats, equipment, quests, reputation.
3. World seed: generator version and canonical region identity.
4. World journal: blocks, entities, event lifecycle, NPC state.
5. Co-op ledger: validated shared discoveries and phantom records.

Every save write is atomic: write temporary file, flush, checksum, rename. Keep two rolling backups and expose recovery in the settings menu.

---

## 15. Legend Engine T architecture

Legend Engine T is a C++17/20 custom engine with platform-neutral simulation.

**Core modules**

- `TCore`: memory, jobs, logging, deterministic random, serialization.
- `TWorld`: voxel chunks, terrain, floating origin, region streaming.
- `TEntity`: ECS pools, components, archetypes, save identity.
- `TPhysics`: character controller, voxel collision, rigid bodies, destruction.
- `TAnim`: state machines, IK, weapon layers, hit frames, additive reactions.
- `TRender`: OpenGL 4.0/3.2 desktop backend and GLES 3 mobile backend.
- `TLight`: HDR, bloom, volumetric fog, god rays, probes, planar reflection.
- `TAudio`: positional sound, biome layers, combat stems, low-memory streaming.
- `TNet`: snapshot replication, matchmaking adapter, async ledger.
- `TTools`: prefab converter, schematic validator, nav baker, profiler, save inspector.

The renderer must degrade by capability tier:

- **Desktop Ultra:** HDR, bloom, volumetric lighting, planar reflections, god rays, animated grass, shadows, contact AO.
- **Desktop Medium:** HDR/bloom, cascaded shadows, fog, simplified reflections.
- **Android C31:** GLES 3, palette lighting, baked probes, impostor vegetation, 30 FPS target, 512–720p internal resolution.

---

## 16. Visual direction

The visual language is “pixel-art discipline applied to 3D”: hard readable silhouettes, limited colour families, stepped highlights, bright Taumium emissive accents, dithered fog, and deliberate texture clusters. It is not a flat 2D mock-up.

Materials need albedo, normal/height where affordable, roughness, emissive, aspect mask, and damage mask. Taumium emissive colour is cyan/teal; flux is magenta-black; ash is amber; frost is blue-white; memory is pale gold.

Animation requirements: locomotion blend tree, armour-specific silhouettes, attack wind-up/contact/recovery markers, hit reactions by posture state, cloth/rope secondary motion, grass vertex sway, spell trails, weapon attachment recoil, and boss phase transformations.

---

## 17. UI and accessibility

Main menu: RDGaming Studios logo, New Journey, Continue, Load World, Multiplayer, Settings, Mod Data, Exit.

HUD: health, stamina, Vis, posture, quick belt, active research, compass, event alert, lock-on indicator, interaction prompt, and optional damage numbers.

Settings: graphics tier, internal resolution, camera distance, motion reduction, colourblind palettes, subtitles, font scale, controller layout, touch layout, vibration, audio mix, and save recovery.

Android controls: left movement stick, right camera pad, attack, guard, dodge, jump, spell, interact, item belt, lock-on. Buttons are resizable and hideable.

---

## 18. Agent team production plan

### Milestone A — Foundation

World coordinates, chunk streaming, save journal, deterministic seed, ECS, input abstraction, test harness, crash logs.

### Milestone B — Vertical slice

One city, one biome, one dungeon, one boss, four weapons, one research chain, one event lifecycle, Windows and Android test builds.

### Milestone C — Combat and content

All weapon families, armour sockets, enemy roles, animation markers, physics destruction, first five bosses, authored dungeon kit.

### Milestone D — World expansion

Eight biomes, five cities, regional events, NPC schedules, faction consequences, async ghosts, Linux headless node.

### Milestone E — Multiplayer and optimisation

Host session, replication, matchmaking adapter, mobile renderer tiers, save migration, profiling, controller/touch QA.

### Milestone F — Release candidate

Content lock, localisation, accessibility, crash-free soak tests, recovery testing, installer/package signing, final credits and RDGaming Studios attribution.

Every agent task must include: files changed, data schema changed, tests run, memory/performance budget, save migration impact, and platform impact.

---

## 19. Verification gates

A feature is not “done” until it passes the relevant gate:

- **Simulation:** deterministic replay produces identical state.
- **World:** unload/reload preserves edits and event lifecycle.
- **Combat:** hit frames, posture, guard, dodge, and death work at 30/60 FPS.
- **Rendering:** no black screen; clear colour, first frame, shader compile, and swap are logged.
- **Save:** interrupted write recovers from backup; version migration tested.
- **Android:** install, cold start, rotate/resize, background/foreground, touch input, and 10-minute soak on Realme C31 class hardware.
- **Linux:** headless node boots, loads a release-compatible save, and runs under systemd/tmux.
- **Windows:** portable build launches with external `data/` and `saves/`, no hidden dependency on source paths.
- **Packaging:** inspect archive, manifest, native ABI, alignment, signature, and asset hashes.

---

## 20. Definition of done

ThaumtMourge is ready for public testing when a new player can install the correct platform package, see RDGaming Studios branding, create a character, enter a unique seeded region, fight and defeat a real boss, research an aspect, modify equipment, trigger and resolve an event, save, close, reload, and observe the same world state. A render screenshot must come from the running build, and the build log must identify the commit, content version, renderer tier, seed, and save schema version.

**Attribution:** ThaumtMourge is developed by RDGaming Studios using Legend Engine T.

---

## 21. Data schemas for agents

### Item record

```json
{
  "id":"weapon.aetherfang",
  "display_name":"Aetherfang",
  "family":"greatblade",
  "damage":{"physical":42,"aether":8},
  "posture":38,
  "vis_cost":8,
  "weight":7.2,
  "sockets":["core","guard","rune"],
  "moveset":"greatblade_02",
  "aspect_tags":["motion","memory"],
  "lore_key":"item.aetherfang.lore"
}
```

### Enemy record

```json
{
  "id":"enemy.frostbound_knight",
  "role":"duelist",
  "perception":26,
  "posture":120,
  "resistances":{"frost":0.8,"ash":1.35},
  "attack_set":"frost_knight_v2",
  "drops":["aspect.form","armor.frostbound_plate"],
  "biomes":["frost_crown"],
  "event_mutations":["revenant","champion","tainted"]
}
```

### Event record

```json
{
  "id":"event.caravan_under_siege",
  "requirements":{"biome":["verdant_reach","salt_dune"],"faction":"free_caravans"},
  "stages":["rumour","ambush","rescue","aftermath"],
  "failure_result":"road_bandit_checkpoint",
  "success_result":"travelling_vendor_and_research_note",
  "cooldown_hours":18
}
```

---

## 22. Controls and input contract

Desktop default: WASD movement, mouse camera, left mouse light attack, right mouse guard, Q dodge, Space jump, E spell, R mine/interact with blocks, B build, V scan, F shrine, H flask, C infusion, G construct command, P leyline travel, T lock-on, I inventory, J journal, K save, L load, number keys quick belt.

Controller actions are mapped by semantic names rather than key codes: `move`, `look`, `attack_light`, `attack_heavy`, `guard`, `dodge`, `jump`, `cast`, `interact`, `lock_on`, `quick_item`, `menu`, and `pause`.

Touch input uses a virtual left stick, camera drag region, and context-sensitive action cluster. Touch buttons must never cover the health/posture HUD, and all buttons require a 9dp minimum hit target after scaling.

---

## 23. Performance budgets

### Android C31 target

- 30 FPS sustained at 720p logical frame, 512–640p internal render.
- 1.5 GB process memory ceiling target.
- 150 visible dynamic actors maximum, with aggressive impostors beyond 40 m.
- 4 active shadow casters, 1 local reflection probe, no planar reflections by default.
- 2 km active terrain horizon using impostor rings.
- Save journal writes batched at 2–5 second intervals and on lifecycle pause.

### Desktop target

- 60 FPS at 1080p on GTX 1060/RX 580 class hardware.
- 120 FPS option with reduced volumetrics and foliage.
- 8–12 GB RAM depending on streaming radius.
- Dedicated streaming and mesh jobs; no blocking disk access on the render thread.

The profiler records frame time by simulation, culling, chunk generation, mesh upload, shadows, lighting, UI, and present. Every release candidate includes a 30-minute traversal capture and a 1000-event stress seed.

---

## 24. Audio direction

The score combines low strings, prepared percussion, bowed metal, distant choir, and broken mechanical pulses. Each biome has exploration, danger, event, boss, and aftermath stems. Aspect research introduces a quiet tonal motif; flux adds detuned granular noise.

Combat audio must communicate attack class without relying only on animation: guardable strikes have a metallic pre-ring, unblockable attacks have a low reversed pulse, perfect deflection has a short glass/metal crack, and posture break has a spatially widening impact.

Android uses Opus streams with a 32-voice mixer cap and loudness normalisation. Desktop can use higher-quality stems and regional ambience.

---

## 25. Modding and external data

Mods live in `mods/<id>/` and may add data records, palettes, recipes, enemy variants, dungeons, dialogue, and event templates. Code plugins are disabled by default on Android and require a signed permission manifest on desktop.

A content validator checks unknown IDs, cycles in recipes, impossible collision cells, oversized textures, missing localisation keys, nav holes, event deadlocks, and save incompatibilities. The validator runs in CI before an agent’s content merge.

---

## 26. Localisation

English is the source language. Russian is the first localisation target. Every dialogue, item, quest, event, and tutorial line uses a stable key. Text must not be embedded in C++ or prefab binary data. Font fallback must support Cyrillic on all platforms.

The game should preserve concise UI labels on mobile. Lore can be long in the journal, but combat prompts remain short: **GUARD**, **DEFLECT**, **SCAN**, **INFUSE**, **REST**, **BUILD**.

---

## 27. Risk register

- **35,000 km scope:** solved by streaming and authored local density; never attempt a full world bake.
- **Android graphics gap:** maintain a GLES tier and a visual test scene independent from the desktop renderer.
- **Destruction and saves:** journal edits with compaction, checksums, and deterministic replay tests.
- **Content explosion:** use biome grammar, event templates, and kitbash constraints; reserve hand-authored work for hubs, dungeons, and bosses.
- **Multiplayer desync:** server/host authority for combat and validated journal events; offline remains first-class.
- **AI agent drift:** source-of-truth schemas, mandatory tests, and review gates in Section 19.
- **Asset rights:** only original, commissioned, procedural, or explicitly licensed assets; no extracted Minecraft or Thaumcraft files.

---

## 28. Agent task template

```text
Task ID:
System:
Player-facing goal:
Files/data touched:
Dependencies:
State and save impact:
Windows impact:
Linux impact:
Android impact:
Performance budget:
Failure cases:
Automated tests:
Manual test route:
Screenshot or replay evidence:
Reviewer:
```

An agent must not mark a task complete without attaching either a test result, a deterministic replay, an inspected save, or a real build screenshot appropriate to the task.
