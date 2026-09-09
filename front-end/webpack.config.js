import path from 'path';
import HtmlWebpackPlugin from 'html-webpack-plugin';
import MiniCssExtractPlugin from 'mini-css-extract-plugin';
import CopyWebpackPlugin from 'copy-webpack-plugin';

export default (env, argv) => {
  const isProduction = argv.mode === 'production';

  return {
    // 1. Multiple entry points matching your HTML files
    entry: {
      'files-upload': path.resolve(
        import.meta.dirname,
        './src/ts/xhrupload-test.ts'
      ),
    },
    // 2. Output targets (Hashed names in production for cache busting)
    output: {
      path: path.resolve(import.meta.dirname, 'dist/static'),
      filename: isProduction ? 'js/[name].[contenthash:8].js' : 'js/[name].js',
      clean: true, // Clears the dist folder before every build
    },

    // 3. Optimization profiles
    mode: argv.mode || 'development',
    devtool: isProduction ? false : 'eval-cheap-module-source-map',

    // 4. Module Resolution rules
    resolve: {
      extensions: ['.ts', '.js'],
    },

    // 5. Build Pipelines (Loaders)
    module: {
      rules: [
        // TypeScript compilation: feeds directly to Microsoft tsc compiler
        {
          test: /\.ts$/,
          loader: 'ts-loader',
          options: {
            compilerOptions: isProduction
              ? {
                  declaration: false, // Disable generation of .d.ts files
                  declarationMap: false, // Disable generation of .d.ts.map files
                  sourceMap: false, // Disable generation of .js.map files
                  noEmit: false,
                }
              : {}, // Keeps your default tsconfig setup active during development
          },
          exclude: /node_modules/,
        },
        // LESS Compilation: parsed bottom-to-top
        {
          test: /\.less$/,
          use: [
            // Inject styles globally in Dev; Extract to a production bundle in Prod
            isProduction ? MiniCssExtractPlugin.loader : 'style-loader',
            'css-loader', // Resolves css imports
            'less-loader', // Compiles LESS syntax to standard CSS
          ],
        },
        {
          test: /\.html$/i,
          loader: 'html-loader',
          options: {
            minimize: false, // 👈 Disables html-loader's built-in minification
          },
        },
      ],
    },

    // 6. Automation & Templating Plugins
    plugins: [
      // Map main entry script to index.html
      new HtmlWebpackPlugin({
        template: './src/index.html',
        filename: 'index.html',
        chunks: ['main'],
        minify: false,
      }),
      // Map about entry script to about.html
      new HtmlWebpackPlugin({
        template: './src/image-upload.html',
        filename: 'image-upload.html',
        chunks: ['files-upload'],
      }),
      new CopyWebpackPlugin({
        patterns: [
          {
            from: path.resolve(
              import.meta.dirname,
              'src/assets/manifest.webmanifest'
            ),
            to: path.resolve(import.meta.dirname, 'dist/static'),
          },
          {
            from: path.resolve(import.meta.dirname, 'src/img/favicon.ico'),
            to: path.resolve(import.meta.dirname, 'dist/static'),
          },
        ],
      }),
      // Extracts CSS into individual files during a production run
      ...(isProduction
        ? [
            new MiniCssExtractPlugin({
              filename: 'css/[name].[contenthash:8].css',
            }),
          ]
        : []),
    ],

    // 7. Webpack Local Development Server Context
    devServer: {
      compress: true,
      port: 3000,
      open: true, // Opens browser instantly
      hot: true, // Live styling updates without dropping runtime states
    },
  };
};
