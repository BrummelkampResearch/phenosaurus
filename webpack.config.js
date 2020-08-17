const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const { CleanWebpackPlugin } = require('clean-webpack-plugin');
const webpack = require('webpack');

const SCRIPTS = __dirname + "/webapp/";
const SCSS = __dirname + "/scss/";
const DEST = __dirname + "/docroot/";

module.exports = {
	mode: "development",
	entry: {
		'sa-style': SCSS + "sa-style.scss",
		'index': SCRIPTS + "index.js",
		'screen': SCRIPTS + "screen.js",
		'sl-screen': SCRIPTS + "sl-screen.js",

		// 'login': SCRIPTS + "login.js",
		// 'geneFinder': SCRIPTS + "geneFinder.js",
		// 'compare': SCRIPTS + "compare.js",
		// 'compare2': SCRIPTS + "compare2.js",
		// 'compare-sbs': SCRIPTS + "compare-sbs.js",

		'admin-user': SCRIPTS + "admin-user.js",
		'admin-group': SCRIPTS + "admin-group.js",
		'admin-screen': SCRIPTS + "admin-screen.js",
		// 'admin-genome': SCRIPTS + "admin-genome.js",
		'user-screen': SCRIPTS + "user-screen.js"
	},

	output: {
		path: DEST,
		filename: "./scripts/[name].js"
	},

	devtool: "source-map",

	module: {
		rules: [
			{
				test: /\.js#/,
				exclude: /node_modules/,
				use: {
					loader: "babel-loader",
					options: {
						presets: ['@babel/preset-env']
					}
				}
			},
			{
				test: /\.css$/,
				use: [
					// 'style-loader',
					MiniCssExtractPlugin.loader,
					'css-loader'
				]
			},
			{
				test: /\.(eot|svg|ttf|woff(2)?)(\?v=\d+\.\d+\.\d+)?/,
				loader: 'file-loader',
				options: {
					name: '[name].[ext]',
					outputPath: 'fonts/',
					publicPath: '../fonts/'
				}
			},
			{
				test: /\.s[ac]ss$/i,
				use: [
					MiniCssExtractPlugin.loader,
					'css-loader',
					'sass-loader'
				]
			},
			{
				test: /\.(png|jpg|gif)$/,
				use: [
					{
						loader: 'file-loader',
						options: {
							outputPath: "css/images",
							publicPath: "images/"
						},
					},
				]
			}
		]
	},

	plugins: [
		new CleanWebpackPlugin({
			dry: true,
			cleanOnceBeforeBuildPatterns: [
				'css/**/*',
				'css/*',
				'scripts/**/*',
				'fonts/**/*'
			]
		}),
		new webpack.ProvidePlugin({
			$: 'jquery',
			jQuery: 'jquery'
		}),
		new MiniCssExtractPlugin({
			filename: './css/[name].css',
			chunkFilename: './css/[id].css'
		})
	]
};
