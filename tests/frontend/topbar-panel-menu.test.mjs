import assert from 'node:assert/strict';
import {
  PANEL_MENU_ITEMS,
  defaultPanelVisibility,
  findDockButton,
  getPanelItem,
  panelKeys,
  panelVisibilityFromClassList,
  readSavedPanelVisibility,
  writeSavedPanelVisibility,
} from '../../GUI/frontend/assets/js/topbarPanelMenuModel.js';

assert.deepEqual(panelKeys(), ['library', 'inspector', 'cost', 'core', 'probe', 'health', 'io']);
assert.equal(getPanelItem('library').label, 'Algorithm Library');
assert.equal(getPanelItem('inspector').hiddenClass, 'right-hidden');
assert.equal(getPanelItem('cost').cardSelector, '.cost-card');
assert.equal(getPanelItem('io').label, 'Audio I/O');

assert.deepEqual(defaultPanelVisibility(), {
  library: true,
  inspector: true,
  cost: true,
  core: true,
  probe: true,
  health: true,
  io: true,
});

assert.deepEqual(
  panelVisibilityFromClassList({ contains: cls => cls === 'right-hidden' }),
  { library: true, inspector: false, cost: true, core: true, probe: true, health: true, io: true }
);

const fakeDoc = {
  querySelectorAll(selector) {
    assert.equal(selector, '.panel-dock button');
    return [
      { textContent: 'Library' },
      { textContent: 'Inspector' },
      { textContent: 'Dashboard' },
    ];
  },
};
assert.equal(findDockButton(fakeDoc, 'Inspector').textContent, 'Inspector');
assert.equal(findDockButton(fakeDoc, 'Missing'), null);

const store = new Map();
const storage = {
  getItem: key => store.get(key) ?? null,
  setItem: (key, value) => store.set(key, value),
};
writeSavedPanelVisibility({ library: true, inspector: false, cost: false, core: true, probe: true, health: true, io: true }, storage);
assert.deepEqual(readSavedPanelVisibility(storage), {
  library: true,
  inspector: false,
  cost: false,
  core: true,
  probe: true,
  health: true,
  io: true,
});

assert.equal(PANEL_MENU_ITEMS.length, 7);
console.log('topbar-panel-menu.test passed');
