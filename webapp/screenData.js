import * as d3 from "d3";
import Dot from "./dot";
import $ from 'jquery';

/* global context_name */

export default class ScreenData {

	constructor(screenName, screenID) {
		this.screenID = screenID;
		this.screenName = screenName;
		this.geneColours = null;
	}

	load(options) {
		return new Promise((resolve, reject) => {
			fetch(`${context_name}ip/screenData/${this.screenID}`, {
				method: "post",
				credentials: "include",
				body: options
			})
				.then(value => {
					// if (value.ok)
					// 	return value.json();
					// if (value.status === 403)
					// 	throw "invalid-credentials";

					return value.json();
				})
				.then(data => {
					if (data.error != null)
						throw data.error;

					this.process(data);
					resolve(this);
				})
				.catch(err => {
					console.log("Error fetching screendata: " + err);

					// if (err === "invalid-credentials")
					// 	showLoginDialog(null, () => this.loadScreen(this.screenName, this.screenID));
					// else
					reject(err);
				});
		});
	}

	process(data) {
		let minMI = 0, maxMI = 0, maxInsertions = 0;
		data.forEach(d => {
			d.insertions = d.low + d.high;
			d.log2mi = Math.log2(d.mi);

			if (minMI > d.log2mi)
				minMI = d.log2mi;
			if (maxMI < d.log2mi)
				maxMI = d.log2mi;
			if (maxInsertions < d.insertions)
				maxInsertions = d.insertions;
			if (this.maxRank < d.rank)
				this.maxRank = d.rank;
		});

		maxInsertions = Math.ceil(Math.pow(10, Math.log10(maxInsertions) + 0.1));

		this.xRange = [1, maxInsertions];
		this.yRange = [Math.floor(minMI), Math.ceil(maxMI)];

		this.data = data;

		this.dotData = d3.nest()
			.key(d => [this.screenID, d.mi, d.low + d.high].join(':'))
			.entries(data)
			.map(d => new Dot(d.key, d.values));
	}

	loadUnique(options) {
		return new Promise((resolve, reject) => {
			if (this.geneColours != null) {
				resolve(null);
				return;
			}

			fetch(`${context_name}ip/unique/${this.screenID}`,
			{
				method: "post",
				credentials: "include",
				body: options
			}).then(data => {
				if (data.ok)
					return data.json();
				if (data.status == 403)
					throw "invalid-credentials";
			}).then(data => {
				this.geneColours = new Map(data.map(d => [d.gene, d.colour]));
				this.data.forEach(d => d.unique = this.geneColours.get(d.geneID));
				resolve(null);
			}).catch(err => {
				return reject(err);
			});
		});
	}
}
