// const MiniCssExtractPlugin = require('mini-css-extract-plugin');
// const { CleanWebpackPlugin } = require('clean-webpack-plugin');
// const webpack = require('webpack');
// const path = require('path');
// const { mode } = require('d3');

// const SCRIPTS = __dirname + "/webapp/";
// const SCSS = __dirname + "/scss/";

const { CleanWebpackPlugin } = require('clean-webpack-plugin');
const path = require('path');
const MiniCssExtractPlugin = require('mini-css-extract-plugin');

const SCRIPTS = __dirname + "/webapp/";
const SCSS = __dirname + "/scss/";
const DEST = __dirname + "/docroot/scripts/";

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
			path: DEST,
			crossOriginLoading: 'anonymous'
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
					}
				},

				{
					test: /\.(sa|sc|c)ss$/i,
					use: [
						PRODUCTION ? MiniCssExtractPlugin.loader : "style-loader",
						"css-loader",
						"postcss-loader",
						"sass-loader"
					]
				},

				{
					test: /\.woff(2)?(\?v=[0-9]\.[0-9]\.[0-9])?$/,
					include: path.resolve(__dirname, './node_modules/bootstrap-icons/font/fonts'),
					type: 'asset/resource',
					generator: {
						filename: '../fonts/[name][ext]'
					}
				}
			]
		},

		resolve: {
			extensions: ['.js', '.scss'],
		},

		plugins: [],

		optimization: {
			minimizer: []
		}
	};

	if (PRODUCTION) {
		webpackConf.mode = "production";

		webpackConf.plugins.push(
			new CleanWebpackPlugin({
				verbose: true
			}),
			new MiniCssExtractPlugin({})
		);
	} else {
		webpackConf.mode = "development";
		webpackConf.devtool = 'source-map';
	}

	return webpackConf;
};

