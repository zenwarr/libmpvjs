import * as React from 'react';
import {PlayerState} from "../../store";
import {observer} from "mobx-react";

interface PlayerProps {
  s: PlayerState;
}

const PROPS_TO_OBSERVE = 'pause fullscreen';

@observer
export class Player extends React.Component<PlayerProps> {
  render() {
    return <div>
      
    </div>;
  }
}
