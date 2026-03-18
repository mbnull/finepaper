# Integration Test with Framework

## Test Configuration

Created a 2×2 mesh NoC with xy routing and 2 endpoints:
- `master_0` at router (0,0)
- `slave_0` at router (1,1)

## Test Process

1. Created `test_config.json` with framework-compatible schema
2. Ran framework generator:
   ```bash
   ../framework/bin/generate -i test_config.json -o test_output -t ../framework/template
   ```

## Results

✓ **PASSED** - Framework successfully generated RTL:
- 4 router modules (xp_router_xp_0_0.sv through xp_router_xp_1_1.sv)
- 1 top-level module (test_noc_top.v)

## Schema Compatibility

Frontend export matches framework's expected JSON structure:
- Required top-level: `name`, `version`, `xps`, `connections`, `endpoints`
- XPs: `id`, `x`, `y`, `endpoints` (array of endpoint IDs), `config` (routing_algorithm, vc_count, buffer_depth)
- Connections: `from`, `to`, `dir`
- Endpoints: `id`, `type`, `protocol`, `data_width`

Schema updated in Round 3 to match framework parser requirements.
