const webpack = require('webpack');
const path = require('path');

module.exports = [
  {
    entry: './src/index.ts',

    output: {
      filename: 'bundle.js',
      path: path.join(__dirname, '/../dist/'),
      libraryTarget: 'commonjs',
      libraryExport: 'default'
    },

    target: 'electron-renderer',

    resolve: {
      extensions: ['.ts', '.tsx', '.js', '.json', '.webpack.js']
    },

    module: {
      rules: [
        {
          test: /\.tsx?$/,
          loader: 'ts-loader'
        },
        {
          test: /\.node$/,
          use: 'node-loader'
        }
      ]
    }
  }
];
