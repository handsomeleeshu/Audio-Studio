export const PANEL_MENU_ITEMS = [
  {
    key: 'library',
    label: 'Algorithm Library',
    hiddenClass: 'left-hidden',
    closeSelector: '.left-panel .panel-toggle',
    dockLabel: 'Library',
    type: 'app-panel',
  },
  {
    key: 'inspector',
    label: 'Inspector',
    hiddenClass: 'right-hidden',
    closeSelector: '.right-panel .panel-toggle',
    dockLabel: 'Inspector',
    type: 'app-panel',
  },
  {
    key: 'cost',
    label: 'Per-Algorithm Cost',
    cardSelector: '.cost-card',
    type: 'dashboard-card',
  },
  {
    key: 'core',
    label: 'DSP Core Loading',
    cardSelector: '.core-card',
    type: 'dashboard-card',
  },
  {
    key: 'probe',
    label: 'Signal Probe',
    cardSelector: '.wave-card',
    type: 'dashboard-card',
  },
  {
    key: 'health',
    label: 'System Health',
    cardSelector: '.health-card',
    type: 'dashboard-card',
  },
  {
    key: 'io',
    label: 'Audio I/O',
    cardSelector: '.io-card',
    type: 'dashboard-card',
  },
];

export function panelKeys() {
  return PANEL_MENU_ITEMS.map(item => item.key);
}

export function getPanelItem(key) {
  return PANEL_MENU_ITEMS.find(item => item.key === key) || null;
}

export function defaultPanelVisibility() {
  return Object.fromEntries(PANEL_MENU_ITEMS.map(item => [item.key, true]));
}

export function panelVisibilityFromClassList(classList) {
  const has = typeof classList?.contains === 'function'
    ? cls => classList.contains(cls)
    : cls => Array.from(classList || []).includes(cls);

  const result = defaultPanelVisibility();
  for (const item of PANEL_MENU_ITEMS.filter(item => item.type === 'app-panel')) {
    result[item.key] = !has(item.hiddenClass);
  }
  return result;
}

export function findDockButton(doc, dockLabel) {
  const buttons = Array.from(doc.querySelectorAll?.('.panel-dock button') || []);
  return buttons.find(button => button.textContent.trim().toLowerCase() === dockLabel.toLowerCase()) || null;
}

export function readSavedPanelVisibility(storage = globalThis.localStorage) {
  try {
    const raw = storage?.getItem?.('audioStudioPanelMenu');
    if (!raw) return null;
    const parsed = JSON.parse(raw);
    return { ...defaultPanelVisibility(), ...Object.fromEntries(PANEL_MENU_ITEMS.map(item => [item.key, parsed[item.key] !== false])) };
  } catch {
    return null;
  }
}

export function writeSavedPanelVisibility(visible, storage = globalThis.localStorage) {
  try {
    storage?.setItem?.('audioStudioPanelMenu', JSON.stringify(visible));
  } catch {
    /* localStorage can be unavailable in private / restricted contexts. */
  }
}
