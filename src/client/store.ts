import {observable} from "mobx";

export interface PlayerState {
  pause: boolean;
  fullscreen: boolean;
}

export interface AppState {
  player: PlayerState;
}

let _appStore = observable<AppState>({
  player: {
    pause: true,
    fullscreen: false
  }
});

export function getStore(): AppState {
  return _appStore;
}
