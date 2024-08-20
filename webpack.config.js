const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const { CleanWebpackPlugin } = require('clean-webpack-plugin');
const webpack = require('webpack');
const path = require('path');
const { mode } = require('d3');

const SCRIPTS = __dirname + "/webapp/";
const SCSS = __dirname + "/scss/";
const DEST = __dirname + "/docroot/";

module.exports = (env) => {

	const PRODUCTION = env != null && env.PRODUCTION;

	const webpackConf = {

		entry: {
			'sa-style': SCSS + "sa-style.scss",

			'index': SCRIPTS + "index.js",
			'screen': SCRIPTS + "screen.js",
			'sl-screen': SCRIPTS + "sl-screen.js",

			'gene-selection': SCRIPTS + 'gene-selection.js',
			'gene-finder': SCRIPTS + 'gene-finder.js',
			'similar-finder': SCRIPTS + 'similar-finder.js',
			'cluster-finder': SCRIPTS + 'cluster-finder.js',

			'compare-3': SCRIPTS + "compare-3.js",

			'admin-user': SCRIPTS + "admin-user.js",
			'admin-group': SCRIPTS + "admin-group.js",
			'genome-browser': SCRIPTS + "genome-browser.js",

			'create-screen': SCRIPTS + "create-screen.js",
			'edit-screen': SCRIPTS + "edit-screen.js",
			'list-screen': SCRIPTS + "list-screen.js",

			'qc': SCRIPTS + "qc.js",

			'sortable': SCRIPTS + "sortable.js"
		},

		output: {
			path: path.resolve(__dirname, "docroot"),
			filename: "scripts/[name].js",
			chunkFilename: "scripts/[id].js"
		},

		module: {
			rules: [
				{
					test: /\.js/,
					exclude: /node_modules/,
					use: {
						loader: "babel-loader",
						options: {
							presets: ['@babel/preset-env']
						}
					},
					generator: {
						filename: 'scripts/[name].js'
					},
				},

				{
					test: /\.(sa|sc|c)ss$/i,
					// exclude: /node_modules/,
					use: [
						PRODUCTION ? MiniCssExtractPlugin.loader : "style-loader",
						"css-loader",
						"postcss-loader",
						"sass-loader"
					],
					generator: {
						filename: 'css/[name].css'
					},
					type: "javascript/auto"
				},

				{
					test: /\.woff(2)?(\?v=[0-9]\.[0-9]\.[0-9])?$/,
					// include: path.resolve(__dirname, './node_modules/bootstrap-icons/font/fonts'),
					type: 'asset/resource',
					generator: {
						filename: 'css/fonts/[name][ext]'
					}
				},

				{
					test: /\.(png|jpg|gif)$/,
					type: 'asset/resource',
					generator: {
						filename: 'css/images/[name][ext]'
					}
				}
			]
		},

		resolve: {
			extensions: ['.js', '.scss'],
		},

		plugins: [
			new MiniCssExtractPlugin({
				filename: "css/[name].css",
				chunkFilename: "css/[id].css"
			})
		],

		optimization: {
			minimizer: [],
			// splitChunks: {
			// 	chunks: 'all'
			// }
		}
	};

	if (PRODUCTION) {
		webpackConf.mode = "production";

		webpackConf.plugins.push(
			new CleanWebpackPlugin({
				cleanOnceBeforeBuildPatterns: [
					'scripts',
					'fonts'
				]
			}));

		// webpackConf.optimization.minimizer.push(
		// 	new TerserPlugin({ /* additional options here */ }),
		// 	new UglifyJsPlugin({ parallel: 4 })
		// );
	} else {
		webpackConf.mode = "development";
		webpackConf.devtool = 'source-map';
		webpackConf.plugins.push(new webpack.optimize.AggressiveMergingPlugin())
	}

	return webpackConf;
};

