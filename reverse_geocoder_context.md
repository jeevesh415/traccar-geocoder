# Reverse Geocoder — Project Context

## Goal

Build a self-hosted, street-level reverse geocoding service. Given (lat, lng), return a full address: house number, street name, city, state, postcode, country.

**Requirements:**
- Global coverage (OSM data)
- Street-level precision with house number interpolation
- High throughput (~20,000–35,000 req/s per node over HTTPS)
- Self-hosted, no external dependencies at query time
- Cost-efficient (~$265/mo for HA 2-node setup)

---

## Architecture Overview

Two completely separate components:

```
[Build Pipeline]  →  binary index files  →  [Query Server]
    (C++)                                       (Rust)
```

They communicate only through files on disk. No shared code required.

---

## Algorithm

### Query Time (Two Independent Lookups)

```
Query (lat, lng)
      │
      ├──► Street lookup
      │      S2 cell hash → 9-cell lookup → nearest way → interpolate house number
      │
      └──► Admin lookup
             S2 cell hash → 9-cell lookup → polygon containment → city/state/postcode
      │
      ▼
Combine → "142 Main St, San Francisco, CA 94103"
```

### Street Lookup Detail

```
1. Hash (lat, lng) to S2 cell ID at level 15 (~300m cells)
2. Compute 8 neighbor cell IDs (arithmetic, no I/O)
3. Fetch candidate ways from all 9 cells (~18 ways total at level 15)
4. For each candidate way:
   - iterate edges (node_i, node_i+1)
   - project query point onto each edge: t = clamp(dot(Q-A, B-A) / dot(B-A, B-A), 0, 1)
   - P = A + t*(B-A)
   - dist = haversine(Q, P)
5. Keep nearest way + best t value
6. Interpolate house number from t + address range
```

### Admin Lookup Detail

```
1. Hash (lat, lng) to S2 cell ID at level 10 (~600m cells)
2. Fetch candidate polygons from 9 cells (~5-20 candidates)
3. Point-in-polygon test (ray casting) for each candidate
4. Group results by admin_level, pick smallest area per level
5. Assemble: city (level 8) + county (level 6) + state (level 4) + country (level 2)
```

### Missing Cell Handling

```rust
for level in [15, 13, 11, 9] {
    let candidates = lookup_9_cells(lat, lng, level);
    if !candidates.is_empty() {
        return full_result(candidates, lat, lng);
    }
}
// fallback: admin only, water check, or no_data
```

---

## S2 Cell Level Choice

Level 15 (~300m cells) is the sweet spot:
- ~2 ways per cell on average in urban areas
- 9 cells × ~2 ways = ~18 candidates total
- Neighbor lookups always sufficient (300m >> typical query-to-street distance)
- Index size manageable in RAM

No adaptive logic needed — always do 9-cell lookup, it's cheap at this level.

---

## Index Structure (On-Disk Binary Files)

Six files, all flat binary, all mmap-friendly. No pointers — only offsets.

### Street Side

**street_cells.bin**
```
sorted array of (cell_id: u64, offset: u32)
binary search to find offset for a given cell_id
```

**street_ways.bin**
```
at each offset: [way_id_count: u16, way_id: u32, ...]
follow offset from street_cells to get way ID list
```

**ways.bin**
```
fixed-size way headers, indexed by way_id:
struct WayHeader {
    node_offset: u32,   // offset into nodes.bin
    node_count: u8,
    name_id: u32,       // offset into strings.bin
    addr_left_from: u32,
    addr_left_to: u32,
    addr_right_from: u32,
    addr_right_to: u32,
    interpolation: u8,  // 0=all, 1=even, 2=odd, 3=alphabetic
}
// way_id * sizeof(WayHeader) = byte offset
```

**nodes.bin**
```
flat array of (lat: f32, lng: f32) pairs
way_header.node_offset points here
```

**strings.bin**
```
flat byte array of null-terminated UTF-8 strings
name_id is byte offset into this array
```

### Admin Side

**admin_cells.bin** — same structure as street_cells.bin
**admin_polygons.bin**
```
struct PolygonHeader {
    vertex_offset: u32,
    vertex_count: u16,
    name_id: u32,
    admin_level: u8,
    area: f32,  // for smallest-area-wins tiebreaking
}
```

### Estimated Sizes

| File | Size |
|---|---|
| street_cells.bin | ~5GB |
| street_ways.bin | ~8GB |
| ways.bin | ~10GB |
| nodes.bin | ~8GB |
| strings.bin | ~400MB |
| admin_cells.bin | ~1GB |
| admin_polygons.bin | ~4GB |
| **Total** | **~36-43GB** |

Fits comfortably in RAM on AX162-R (256GB). Fits on AX102 (128GB) with ~85GB to spare.

---

## Build Pipeline (C++)

### Tools
- **libosmium** — PBF parsing, node location resolution, polygon reconstruction
- **S2 geometry (native C++)** — S2RegionCoverer for cell coverage
- Input: `planet.osm.pbf` (~85GB compressed)
- Runs weekly on a dedicated build machine

### Multi-Pass Requirement

OSM PBF is ordered: nodes → ways → relations. Ways only store node IDs, not coordinates. libosmium's location handler solves this transparently:

```cpp
osmium::index::map::SparseMemArray<...> index;
osmium::handler::NodeLocationsForWays<decltype(index)> location_handler{index};
osmium::apply(reader, location_handler, your_handler);
// your way() handler receives fully resolved geometry
```

Node storage options:
- `SparseMemArray` — RAM only, fast, needs ~80GB for planet
- `SparseFile` — disk-based, slower, works with any RAM

### Admin Polygon Reconstruction

Relations require `osmium::area::MultipolygonManager` — specific two-pass setup, see libosmium examples. Not complicated but must use the right tool.

### Build Steps

```
planet.osm.pbf
      │
      ▼
Pass 1 (libosmium MultipolygonManager prep pass)
      │
      ▼
Pass 2 (main processing):

  highway ways:
    - filter: has "highway" tag + has "name" tag
    - exclude: footway, path, track, steps, cycleway (unless needed)
    - resolve geometry (libosmium location handler)
    - for each edge (node_i, node_i+1):
        cells = S2RegionCoverer(edge, fixed_level=15)
        street_index[cell].append(way_id)
    - intern street name → string pool
    - store WayHeader + nodes

  admin relations:
    - filter: boundary=administrative + has admin_level
    - reconstruct polygon (MultipolygonManager)
    - simplify vertices (Douglas-Peucker, ~500 vertices max)
    - cells = S2RegionCoverer(polygon, max_level=10, max_cells=50)
    - admin_index[cell].append(polygon_id)
    - store PolygonHeader + vertices
      │
      ▼
String deduplication:
    unordered_map<string, uint32_t> — intern table
    write deduplicated strings.bin
      │
      ▼
Serialize all files (offset-based, no pointers, f32 coordinates)
      │
      ▼
Verify:
    - checksum each file
    - spot check N random coordinates return plausible results
    - verify no null results in known dense urban areas
      │
      ▼
rsync to serving nodes (~36GB transfer)
```

### Build Time
~2-4 hours on AX102 (16 cores, NVMe). Runs weekly. Serving nodes never touch PBF.

---

## Query Server (Rust)

### Dependencies
- `axum` + `tokio` — HTTP server
- No S2 library needed — only cell ID computation required at query time (~30 lines of math ported from S2 source)
- No other geo dependencies

### S2 Cell ID at Query Time

Only one S2 operation needed: hash (lat, lng) → cell ID. This is pure math, ~30 lines, no library:

```rust
fn lat_lng_to_cell_id(lat: f64, lng: f64, level: u8) -> u64 {
    // project to S2 cube face
    // apply Hilbert curve mapping
    // encode level in low bits
    // ~30 lines, port from s2geometry source
}

fn cell_neighbors(cell_id: u64) -> [u64; 8] {
    // arithmetic on cell ID bits
}
```

### mmap Access Pattern

```rust
// startup: mmap all index files
let street_cells = mmap("street_cells.bin");  // instant
let ways = mmap("ways.bin");
// etc. — OS pages on demand, no cold-start load time

// query hot path — zero allocation
fn query(lat: f64, lng: f64) -> Address {
    let cell = lat_lng_to_cell_id(lat, lng, 15);
    let neighbors = cell_neighbors(cell);
    
    let mut best_dist = f32::MAX;
    let mut best_way: Option<&WayHeader> = None;
    let mut best_t = 0.0f32;
    
    for c in [cell].iter().chain(neighbors.iter()) {
        let offset = binary_search(&street_cells, c);
        let way_ids = &street_ways[offset..];
        for way_id in way_ids {
            let way = &ways[way_id];
            let (dist, t) = nearest_point_on_polyline(way, lat, lng);
            if dist < best_dist {
                best_dist = dist;
                best_way = Some(way);
                best_t = t;
            }
        }
    }
    
    let street_name = &strings[best_way.name_id..];
    let house_number = interpolate(best_way, best_t);
    let admin = admin_lookup(lat, lng);  // same pattern, separate index
    
    Address { house_number, street_name, ..admin }
}
```

### Zero-Downtime Index Updates

```
1. Build machine writes new index files
2. rsync to serving node as index.new.*
3. rename() atomically swaps files (Linux atomic)
4. Send SIGUSR1 to server process
5. Server re-mmaps new files, swaps pointer atomically
6. Old mmap released, OS reclaims pages
```

With two nodes behind load balancer: drain one node, reload, re-add, repeat. No double-RAM needed.

---

## Infrastructure

### Recommended Setup (HA Production)

| Component | Spec | Cost/mo |
|---|---|---|
| 2× AX162-R geo nodes | AMD EPYC 9454P, 48 cores, 256GB RAM, 2×1.92TB NVMe | ~$430 |
| 1× LB node | Small VPS | ~$5 |
| **Total** | | **~$435/mo** |

Capacity: ~30,000–50,000 req/s (HTTPS) → ~2,000–3,000 customers at 15 req/s each.

### Alternative (Budget)

2× AX102 (Ryzen 9 7950X3D, 16 cores, 128GB RAM) at ~$265/mo. Index fits with ~85GB headroom. Lower throughput but much cheaper.

### Build Machine

Separate from serving nodes. Run on-demand (weekly):
- AX102 or equivalent
- Or spin up cloud instance for ~$2-3/run

---

## Data Sources

- **Streets + admin boundaries:** OSM planet PBF from planet.openstreetmap.org or Geofabrik mirrors
- **Water polygons:** Natural Earth (~25MB, for ocean/water detection)
- **Update frequency:** Weekly full rebuild from latest planet PBF

---

## Key Design Decisions Summary

| Decision | Choice | Reason |
|---|---|---|
| Spatial index | S2 at level 15 | Uniform cell areas globally, ~18 candidates per 9-cell lookup |
| Admin index level | S2 at level 10 | Coarser sufficient for large polygons |
| Storage | mmap flat binary files | Zero-copy, instant startup, shared across processes |
| Coordinate precision | f32 | ~1m precision, sufficient, half the size of f64 |
| Way splitting | None | Not needed for reverse geocoding |
| Admin lookup | Offline precompute per segment | Reduces query-time work |
| Updates | Weekly full rebuild | Streets change slowly, simple > complex |
| Caching | None | In-memory index is already sub-ms, cache adds latency |

---

## What You Are NOT Building

- Forward geocoding (address → coordinates)
- Routing
- POI search
- Real-time OSM updates (daily diffs)
- Distributed/sharded index (single node handles full planet)

---

## Open Questions / Future Work

- House number accuracy: per-block address ranges vs whole-way interpolation
- Building footprint layer for rooftop-level precision (Google's ROOFTOP result type)
- Confidence scoring per result
- Daily diff updates if weekly staleness becomes a customer issue
