/// <reference types="react" />

declare module "mpv.js" {
  const PLUGIN_MIME_TYPE: string;
  function getPluginEntry(pluginDir: string, pluginName?: string): string;

  interface ReactMpvProps {
    className?: string;
    style?: { [name: string]: any };
    onReady?: (component: ReactMPV) => void;
    onPropertyChange?: (name: string, value: any) => void;
  }

  class ReactMPV extends React.PureComponent<ReactMpvProps> {
    command(cmd: string, ...args: any[]): void;
    property(name: string, value: any): void;
    observe(name: string): void;
    fullscreen(): void;
    destroy(): void;
    node(): void;
  }
}
