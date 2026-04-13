/**
 * Preload script - Exposes safe IPC methods to renderer
 */
const { contextBridge, ipcRenderer } = require('electron');

// Expose protected methods to renderer
contextBridge.exposeInMainWorld('electronAPI', {
  // Get app version
  getVersion: () => ipcRenderer.invoke('app:version'),
  
  // Get platform info
  getPlatform: () => process.platform,
  
  // Open external links safely
  openExternal: (url) => ipcRenderer.invoke('shell:openExternal', url),
  
  // Window controls
  minimizeWindow: () => ipcRenderer.invoke('window:minimize'),
  maximizeWindow: () => ipcRenderer.invoke('window:maximize'),
  closeWindow: () => ipcRenderer.invoke('window:close'),
  
  // Check if running in Electron
  isElectron: true,
  
  // Listen for backend status updates
  onBackendStatus: (callback) => {
    ipcRenderer.on('backend:status', (event, status) => callback(status));
  }
});

console.log('[Preload] Script loaded successfully');
