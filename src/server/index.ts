import { app, BrowserWindow } from 'electron';
import * as path from 'path';

let mainWin: Electron.BrowserWindow|null = null;

function createWindow() {
  mainWin = new BrowserWindow({
    width: 800,
    height: 600,
    webPreferences: {
      plugins: true
    }
  });

  const rPath = path.join(__dirname, '../client/index.html');
  mainWin.loadURL(path.join('file://', rPath));
  mainWin.webContents.openDevTools();

  mainWin.on('closed', () => {
    mainWin = null;
  });
}

app.on('ready', createWindow);

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('activate', () => {
  if (mainWin == null) {
    createWindow();
  }
});

