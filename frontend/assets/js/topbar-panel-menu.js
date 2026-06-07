import {
  PANEL_MENU_ITEMS,
  defaultPanelVisibility,
  findDockButton,
  getPanelItem,
  panelVisibilityFromClassList,
  readSavedPanelVisibility,
  writeSavedPanelVisibility,
} from './topbarPanelMenuModel.js';

const MENU_ID = 'audio-studio-panel-menu';
const BUTTON_ID = 'audio-studio-panel-menu-button';
let observerStarted = false;
let applying = false;

function currentVisibility() {
  const shell = document.querySelector('.app-shell');
  const appPanelVisibility = shell ? panelVisibilityFromClassList(shell.classList) : defaultPanelVisibility();
  const saved = readSavedPanelVisibility() || {};
  const result = { ...appPanelVisibility, ...saved };

  for (const item of PANEL_MENU_ITEMS.filter(item => item.type === 'dashboard-card')) {
    const card = document.querySelector(item.cardSelector);
    if (card) result[item.key] = !card.classList.contains('dashboard-card-hidden');
  }

  return result;
}

function createMenu() {
  const menu = document.createElement('div');
  menu.id = MENU_ID;
  menu.className = 'floating-menu panel-visibility-menu';
  menu.innerHTML = [
    '<h4>显示 / 隐藏子窗口</h4>',
    ...PANEL_MENU_ITEMS.map(item => (
      `<label>${item.label}<input type="checkbox" data-panel="${item.key}" checked></label>`
    )),
  ].join('');

  menu.addEventListener('change', event => {
    const target = event.target;
    if (!(target instanceof HTMLInputElement)) return;
    setPanelVisible(target.dataset.panel, target.checked);
  });

  return menu;
}

function createButton() {
  const button = document.createElement('button');
  button.id = BUTTON_ID;
  button.className = 'menu-btn panel-menu-btn';
  button.type = 'button';
  button.title = '显示 / 隐藏子窗口';
  button.setAttribute('aria-label', '显示 / 隐藏子窗口');
  button.textContent = '☰';

  button.addEventListener('click', event => {
    event.stopPropagation();
    ensureTopbarMenu();
    const menu = document.getElementById(MENU_ID);
    menu?.classList.toggle('show');
    button.classList.toggle('active', !!menu?.classList.contains('show'));
    syncMenuChecks();
  });

  return button;
}

function ensureTopbarMenu() {
  const topbar = document.querySelector('.topbar');
  if (!topbar) return false;

  let button = document.getElementById(BUTTON_ID);
  if (!button || !topbar.contains(button)) {
    button = createButton();
    const status = topbar.querySelector('.run-badge');
    topbar.insertBefore(button, status || null);
  }

  let menu = document.getElementById(MENU_ID);
  if (!menu) {
    menu = createMenu();
    document.body.appendChild(menu);
  }

  applySavedVisibility();
  syncMenuChecks();
  return true;
}

function syncMenuChecks() {
  const visible = currentVisibility();
  const menu = document.getElementById(MENU_ID);
  if (!menu) return;
  for (const item of PANEL_MENU_ITEMS) {
    const input = menu.querySelector(`input[data-panel="${item.key}"]`);
    if (input) input.checked = visible[item.key] !== false;
  }
}

function setPanelVisible(key, visible) {
  const item = getPanelItem(key);
  if (!item || applying) return;

  if (item.type === 'dashboard-card') {
    const card = document.querySelector(item.cardSelector);
    if (card) card.classList.toggle('dashboard-card-hidden', !visible);
    persist();
    syncMenuChecks();
    return;
  }

  const now = currentVisibility();
  if (now[key] === visible) {
    persist();
    syncMenuChecks();
    return;
  }

  if (visible) {
    const dockButton = findDockButton(document, item.dockLabel);
    dockButton?.click();
  } else {
    document.querySelector(item.closeSelector)?.click();
  }

  setTimeout(() => {
    persist();
    syncMenuChecks();
  }, 0);
}

function persist() {
  writeSavedPanelVisibility(currentVisibility());
}

function applySavedVisibility() {
  const saved = readSavedPanelVisibility();
  if (!saved || applying) return;

  applying = true;
  try {
    for (const item of PANEL_MENU_ITEMS) {
      if (!(item.key in saved)) continue;
      if (item.type === 'dashboard-card') {
        document.querySelector(item.cardSelector)?.classList.toggle('dashboard-card-hidden', saved[item.key] === false);
      } else {
        const now = currentVisibility();
        if (now[item.key] !== saved[item.key]) {
          if (saved[item.key]) findDockButton(document, item.dockLabel)?.click();
          else document.querySelector(item.closeSelector)?.click();
        }
      }
    }
  } finally {
    applying = false;
  }
}

function startObserver() {
  if (observerStarted) return;
  observerStarted = true;

  const observer = new MutationObserver(() => {
    ensureTopbarMenu();
  });
  observer.observe(document.body, { childList: true, subtree: true, attributes: true, attributeFilter: ['class'] });

  document.addEventListener('click', event => {
    const menu = document.getElementById(MENU_ID);
    const button = document.getElementById(BUTTON_ID);
    if (!menu || !button) return;
    if (menu.contains(event.target) || button.contains(event.target)) return;
    menu.classList.remove('show');
    button.classList.remove('active');
  });
}

function boot() {
  if (ensureTopbarMenu()) startObserver();
  else requestAnimationFrame(boot);
}

boot();
