const { CleanWebpackPlugin } = require('clean-webpack-plugin');
const path = require('path');
const glob = require('glob-all');
const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const { PurgeCSSPlugin } = require("purgecss-webpack-plugin");
const TerserPlugin = require("terser-webpack-plugin");
const CopyPlugin = require('copy-webpack-plugin');

const SCRIPTS = path.resolve(__dirname, "webapp");
const DEST = path.resolve(__dirname, "docroot");

module.exports = (env) => {

	const PRODUCTION = env != null && env.PRODUCTION;

	const webpackConf = {

		entry: {
			'style': path.resolve(SCRIPTS, 'sa-style.scss'),

			'index': path.resolve(SCRIPTS, "index.js"),
			'screen': path.resolve(SCRIPTS, "screen.js"),
			'sl-screen': path.resolve(SCRIPTS, "sl-screen.js"),

			'gene-selection': path.resolve(SCRIPTS, 'gene-selection.js'),
			'find-gene': path.resolve(SCRIPTS, 'find-gene.js'),
			'find-similar': path.resolve(SCRIPTS, 'find-similar.js'),
			'find-cluster': path.resolve(SCRIPTS, 'find-cluster.js'),

			'compare-screens': path.resolve(SCRIPTS, "compare-screens.js"),

			'admin-user': path.resolve(SCRIPTS, "admin-user.js"),
			'admin-group': path.resolve(SCRIPTS, "admin-group.js"),
			'genome-browser': path.resolve(SCRIPTS, "genome-browser.js"),

			'create-screen': path.resolve(SCRIPTS, "create-screen.js"),
			'edit-screen': path.resolve(SCRIPTS, "edit-screen.js"),
			'list-screen': path.resolve(SCRIPTS, "list-screen.js"),

			'qc': path.resolve(SCRIPTS, "qc.js"),

			'sortable': path.resolve(SCRIPTS, "sortable.js")
		},

		output: {
			path: DEST,
			crossOriginLoading: 'anonymous',
			filename: "scripts/[name].js",
			clean: {
				dry: true,
				keep(asset) {
					return /\.(html|ico|xml|json)$/.test(asset)
				}
			}
		},

		module: {
			rules: [
				{
					test: /\.js$/,
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
						MiniCssExtractPlugin.loader,
						"css-loader",
						{
							loader: "sass-loader",
							options: {
								sassOptions: {
									silenceDeprecations: [
										"color-functions",
										"global-builtin",
										"import",
										"if-function"
									]
								}
							}
						}
					]
				},

				{
					test: /\.(woff(2)?|ttf)(\?v=[0-9]\.[0-9]\.[0-9])?$/,
					include: [
						path.resolve(__dirname, './node_modules/bootstrap-icons/font/fonts'),
						path.resolve(SCRIPTS, 'fonts')
					],
					type: 'asset/resource',
					generator: {
						filename: 'fonts/[name][ext]'
					}
				},

				{
					test: /\.(png|ico)/,
					include: [
						path.resolve(__dirname, 'images')
					],
					type: 'asset/resource',
					generator: {
						filename: 'images/[name][ext]'
					}
				}
			]
		},

		resolve: {
			extensions: ['.js', '.css', '.scss'],
		},

		optimization: {
			minimize: true,
			minimizer: [new TerserPlugin()]
		},

		target: 'web',

		plugins: [
			new MiniCssExtractPlugin({
				filename: "css/[name].css"
			}),
			new PurgeCSSPlugin({
				paths: glob.sync([
					`${SCRIPTS}/**/*`,
					`${DEST}/**/*`
				], { nodir: true })
			}),
			new CopyPlugin(
				{
					patterns: [
						{
							from: path.resolve(__dirname, 'favicons'),
							to: `${DEST}`
						}
					]
				}
			)
		]
	};

	if (PRODUCTION) {
		webpackConf.mode = "production";
	} else {
		webpackConf.mode = "development";
		webpackConf.devtool = 'source-map';
	}

	return webpackConf;
};
