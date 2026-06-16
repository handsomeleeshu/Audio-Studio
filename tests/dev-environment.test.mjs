import { strict as assert } from 'assert';
import fs from 'fs';

const readJson = path => JSON.parse(fs.readFileSync(new URL(`../${path}`, import.meta.url), 'utf8'));
const readText = path => fs.readFileSync(new URL(`../${path}`, import.meta.url), 'utf8');

const presets = readJson('CMakePresets.json');
const rootCmake = readText('CMakeLists.txt');
const configurePresets = new Map(presets.configurePresets.map(preset => [preset.name, preset]));
const buildPresets = new Set(presets.buildPresets.map(preset => preset.name));

assert.ok(rootCmake.includes('add_subdirectory(GUI/backend)'), 'root CMake entry must include GUI/backend for presets');

for (const name of ['debug', 'release', 'profile']) {
  assert.ok(configurePresets.has(name), `missing CMake configure preset ${name}`);
  assert.ok(buildPresets.has(name), `missing CMake build preset ${name}`);
}

assert.equal(configurePresets.get('debug').cacheVariables.CMAKE_BUILD_TYPE, 'Debug');
assert.equal(configurePresets.get('profile').cacheVariables.CMAKE_BUILD_TYPE, 'RelWithDebInfo');
assert.match(
  configurePresets.get('profile').cacheVariables.CMAKE_CXX_FLAGS_RELWITHDEBINFO,
  /-fno-omit-frame-pointer/,
  'profile preset must keep frame pointers for sampling'
);

const launch = readJson('.vscode/launch.json');
const launchNames = new Set(launch.configurations.map(config => config.name));
const compoundNames = new Set(launch.compounds.map(compound => compound.name));

for (const name of ['Backend: Debug Server', 'Backend: Profile', 'Frontend: Debug Chrome', 'Frontend: Profile Chrome']) {
  assert.ok(launchNames.has(name), `missing launch configuration ${name}`);
}

assert.ok(compoundNames.has('Full Stack: Debug'), 'missing full stack debug compound');
assert.ok(compoundNames.has('Full Stack: Profile'), 'missing full stack profile compound');

const backendDebug = launch.configurations.find(config => config.name === 'Backend: Debug Server');
assert.equal(backendDebug.type, 'lldb');
assert.equal(backendDebug.preLaunchTask, 'cmake: build debug');

const frontendDebug = launch.configurations.find(config => config.name === 'Frontend: Debug Chrome');
assert.equal(frontendDebug.type, 'chrome');
assert.equal(frontendDebug.preLaunchTask, 'wait: backend');

const tasks = readJson('.vscode/tasks.json');
const taskLabels = new Set(tasks.tasks.map(task => task.label));
for (const label of ['cmake: build debug', 'cmake: build profile', 'wait: backend', 'profile: backend']) {
  assert.ok(taskLabels.has(label), `missing VSCode task ${label}`);
}

const guide = readText('docs/debug_profile_guide.md');
for (const phrase of ['Full Stack: Debug', 'Full Stack: Profile', 'Release Safety', '--profile gui_backend']) {
  assert.ok(guide.includes(phrase), `debug/profile guide should mention ${phrase}`);
}

console.log('dev-environment.test passed');
