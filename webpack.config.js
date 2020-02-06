const CleanWebpackPlugin = require('clean-webpack-plugin');
const SCRIPTS = __dirname + "/webapp/";
const DEST = __dirname + "/docroot/scripts/";
const webpack = require('webpack');

module.exports = {
	mode: "development",
	entry: {
		'index': SCRIPTS + "index.js",
		// 'login': SCRIPTS + "login.js",
		// 'geneFinder': SCRIPTS + "geneFinder.js",
		'screen': SCRIPTS + "screen.js",
		// 'compare': SCRIPTS + "compare.js",
		// 'compare2': SCRIPTS + "compare2.js",
		// 'compare-sbs': SCRIPTS + "compare-sbs.js",
		// 'sl-screen': SCRIPTS + "sl-screen.js",

		// 'admin-user': SCRIPTS + "admin-user.js",
		// 'admin-group': SCRIPTS + "admin-group.js",
		// 'admin-genome': SCRIPTS + "admin-genome.js",
		// 'admin-screen': SCRIPTS + "admin-screen.js",
		// 'user-screen': SCRIPTS + "user-screen.js"
	},
	output: {
		path: DEST,
		filename: "[name].js"
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
				use: ['style-loader', 'css-loader']
			},
			{
				test: /\.(eot|svg|ttf|woff(2)?)(\?v=\d+\.\d+\.\d+)?/,
				loader: 'file-loader',
				options: {
					name: '[name].[ext]',
					outputPath: '../fonts/',
					publicPath: 'fonts/'
				}
			},
			{
				test: /\.(png|jpg|gif)$/,
				use: [
					{
						loader: 'file-loader',
						options: {
							outputPath: "../images",
							publicPath: "images/"
						},
					},
				],
			}
		]
	},

	plugins: [
		new CleanWebpackPlugin([DEST]),
		new webpack.ProvidePlugin({
			$: 'jquery',
			jQuery: 'jquery'
		})
	]
};
