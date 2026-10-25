# Writes dependencies.lock for the idf-component-manager 1.5.x that PlatformIO pins: its solver
# cannot parse newer registry metadata (lvgl >= 9.6 'if' rules) and fails on src/idf_component.yml.
# When changing a version in src/idf_component.yml, update the matching entry below (version and
# component_hash from https://components.espressif.com/api/components/<namespace>/<name>) and run:
#   IDF_PATH=~/.platformio/packages/framework-espidf IDF_TARGET=esp32 \
#   ~/.platformio/penv/.espidf-5.4.0/bin/python scripts/mklock.py .
# The manifest hash covers the only component with a manifest, PlatformIO's "src" component.
import sys
import yaml
from idf_component_tools.manifest import ManifestManager
from idf_component_tools.manifest.manifest import ProjectRequirements
from idf_component_tools.lock.manager import LOCK_SCHEMA

proj = sys.argv[1]
m = ManifestManager(proj + '/src', 'src', expand_environment=True, process_opt_deps=True).load()
h = ProjectRequirements([m]).manifest_hash
svc = {'type': 'service', 'service_url': 'https://api.components.espressif.com/'}
lock = {
    'dependencies': {
        'espressif/cmake_utilities': {'component_hash': '351350613ceafba240b761b4ea991e0f231ac7a9f59a9ee901f751bddc0bb18f', 'source': dict(svc), 'version': '0.5.3'},
        'espressif/esp_lcd_st7796': {'component_hash': 'cf252db91cd7b0928b0e03916047d1b90fde8502f9ed00328aa22587d37246a1', 'source': dict(svc), 'version': '1.5.0'},
        'espressif/esp_lvgl_port': {'component_hash': 'd3c020b45c3dfc0d1706a83b63a1c34a6eb444a6e935f9afff79f92b7b94dcef', 'source': dict(svc), 'version': '2.9.0'},
        'lvgl/lvgl': {'component_hash': '184e532558c1c45fefed631f3e235423d22582aafb4630f3e8885c35281a49ae', 'source': dict(svc), 'version': '9.5.0'},
        'idf': {'component_hash': None, 'source': {'type': 'idf'}, 'version': '5.4.0'},
    },
    'manifest_hash': h,
    'target': 'esp32',
    'version': '1.0.0',
}
LOCK_SCHEMA.validate(lock)
with open(proj + '/dependencies.lock', 'w') as f:
    yaml.safe_dump(lock, f)
print('manifest_hash', h)
