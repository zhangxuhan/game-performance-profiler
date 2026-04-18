/**
 * Electron main process - Game Performance Profiler
 * Combines backend server + frontend display in one app
 */
const { app, BrowserWindow, ipcMain, Menu, shell, dialog } = require('electron');
const path = require('path');

// Import and start backend server
const { startServer } = require('./src/server');

let mainWindow = null;

// Detect development mode: use app.isPackaged (most reliable)
const isDev = !app.isPackaged;

function getFrontendUrl() {
  if (isDev) {
    return 'http://localhost:3000';
  }
  // In production, backend serves the frontend
  return 'http://localhost:8080';
}

// Create main window
function createWindow() {
  console.log('[Electron] Creating main window...');
  
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    minWidth: 1000,
    minHeight: 700,
    title: 'Game Performance Profiler',
    backgroundColor: '#1a1a2e',
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      preload: path.join(__dirname, 'preload.js'),
      webSecurity: false, // Allow localhost connections
      allowRunningInsecureContent: true
    },
    autoHideMenuBar: false
  });

  // Create application menu
  const menuTemplate = [
    {
      label: 'File',
      submenu: [
        {
          label: 'Open DevTools',
          accelerator: 'CmdOrCtrl+Shift+I',
          click: () => mainWindow.webContents.openDevTools()
        },
        { type: 'separator' },
        {
          label: 'Exit',
          accelerator: 'CmdOrCtrl+Q',
          click: () => app.quit()
        }
      ]
    },
    {
      label: 'View',
      submenu: [
        { role: 'reload' },
        { role: 'forceReload' },
        { type: 'separator' },
        { role: 'resetZoom' },
        { role: 'zoomIn' },
        { role: 'zoomOut' },
        { type: 'separator' },
        { role: 'togglefullscreen' }
      ]
    },
    {
      label: 'Help',
      submenu: [
        {
          label: 'About',
          click: () => {
            dialog.showMessageBox(mainWindow, {
              type: 'info',
              title: 'About',
              message: 'Game Performance Profiler',
              detail: 'Version 1.0.0\nA tool for profiling game performance metrics with FPS, memory, and frame time monitoring.'
            });
          }
        },
        {
          label: 'Learn More',
          click: () => shell.openExternal('https://github.com')
        }
      ]
    }
  ];

  const menu = Menu.buildFromTemplate(menuTemplate);
  Menu.setApplicationMenu(menu);

  // Load the app
  const frontendUrl = getFrontendUrl();
  console.log('[Electron] Loading:', frontendUrl);
  mainWindow.loadURL(frontendUrl);

  // Window events
  mainWindow.on('closed', () => {
    mainWindow = null;
  });

  mainWindow.webContents.on('did-fail-load', (event, errorCode, errorDesc) => {
    console.error('[Electron] Failed to load:', errorDesc, '(code:', errorCode + ')');
  });

  mainWindow.webContents.on('render-process-gone', (event, details) => {
    console.error('[Electron] Render process gone:', details.reason);
  });

  // Open DevTools in dev mode
  if (isDev) {
    mainWindow.webContents.openDevTools();
  }

  console.log('[Electron] Window created successfully');
}

// IPC handlers for preload
ipcMain.handle('app:version', () => app.getVersion());
ipcMain.handle('shell:openExternal', async (event, url) => {
  await shell.openExternal(url);
});
ipcMain.handle('window:minimize', () => mainWindow?.minimize());
ipcMain.handle('window:maximize', () => {
  if (mainWindow?.isMaximized()) {
    mainWindow.unmaximize();
  } else {
    mainWindow?.maximize();
  }
});
ipcMain.handle('window:close', () => mainWindow?.close());

// App ready
app.whenReady().then(() => {
  console.log('[Electron] App ready, initializing...');
  
  // Start backend server (embedded)
  process.env.ELECTRON = 'true';
  startServer();
  
  // Small delay to let server start
  setTimeout(createWindow, 1500);

  // macOS activation
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

// Quit when all windows are closed
app.on('window-all-closed', () => {
  console.log('[Electron] All windows closed');
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

// Cleanup on quit
app.on('before-quit', () => {
  console.log('[Electron] App quitting...');
});

// Handle uncaught exceptions
process.on('uncaughtException', (error) => {
  console.error('[Electron] Uncaught exception:', error);
});

process.on('unhandledRejection', (reason) => {
  console.error('[Electron] Unhandled rejection:', reason);
});
