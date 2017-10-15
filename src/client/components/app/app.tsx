import * as React from 'react';
import {observer} from "mobx-react";
import {AppState} from "../../store";
import {Player} from "../player/player";
import {remote} from 'electron';
import DevTools from "mobx-react-devtools";
require('./app.scss');

interface AppProps {
  s: AppState;
}

@observer
export class App extends React.Component<AppProps> {
  render() {
    return <div className="app">
      <Player s={this.props.s.player} />

      <div>
        <button onClick={this.loadFile.bind(this)}>Load file...</button>
        <button onClick={this.togglePause.bind(this)}>Play/pause</button>
        <button onClick={this.goFullScreen.bind(this)}>Go fullscreen</button>
      </div>

      <DevTools />
    </div>
  }

  loadFile() {
    let files = remote.dialog.showOpenDialog({

    });
  }

  togglePause() {
    
  }

  goFullScreen() {
    
  }
}
