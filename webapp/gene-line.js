import * as d3 from "d3";
import { geneSelectionEditor } from './gene-selection';
import { Plot, DotPlot, LabelPlot, HeatMapPlot, Screens } from './finder.js';
let nextGeneLineID = 1000;
let geneLines = [];

export class GeneLine {
	constructor(gene) {
		const template = document.querySelector("#gene-row-template");
		const clone = template.content.cloneNode(true);
		const line = clone.querySelector("tr");
		const id = 'gene-' + nextGeneLineID++;

		line.setAttribute("id", id);
		line.geneLine = this;
		document.querySelector("#plot").appendChild(line);

		this.line = line;
		this.data = [];

		[...line.querySelectorAll("a")].forEach(a => a.addEventListener('click', () => this.sort()));

		this.input = line.querySelector("input");
		if (this.input) {
			this.input.addEventListener('change', () => this.changed());
			this.input.focus();
		}

		const td = d3.select(line.querySelectorAll("td")[2]);

		this.heatMap = new HeatMapPlot(td);
		this.heatMap.recreateSVG();

		this.dotPlot = new DotPlot(td);
		this.dotPlot.recreateSVG();

		if (gene === null || gene === undefined)
			gene = this.input ? this.input.value : "";

		if (gene !== "")
			this.setGene(gene);

		geneLines.push(this);
	}

	async changed() {
		const genes = this.input.value.split(/[ \t\r\n,;]+/).filter(e => e.length > 0);

		this.input.classList.remove("gene-not-found");

		if (genes.length > 0) {
			const gene = genes[0];
			this.input.value = gene;

			const options = geneSelectionEditor.getOptions();

			try {
				const resp = await fetch(`finder/${gene}`, {
					method: 'post',
					credentials: "include",
					body: options
				});

				if (resp.ok == false) {
					const msg = await resp.json();
					throw msg.error;
				}

				const data = await resp.json();

				if (data.length === 0)
					this.input.classList.add("gene-not-found");

				this.data = data;

				Plot.preProcessData(data);

				this.heatMap.processData(data, gene);
				this.dotPlot.processData(data, gene);
			} catch (error) {
				this.input.classList.add("gene-not-found");
				console.log(error)
			}

			genes.splice(0, 1);
			genes.forEach(id => new GeneLine(id));
		}
	}

	setGene(id) {
		this.input.value = id;
		this.changed();
	}

	orderedScreens() {
		return this.data
			.sort((a, b) => b.y - a.y)
			.map(a => a.screen);
	}

	sort() {
		const newOrder = this.orderedScreens();

		Screens.instance().reorder(newOrder);

		[...document.querySelectorAll("#plot > tr")]
			.forEach((e) => {
				const geneLine = e.geneLine;
				geneLine.rearrange();
			});

		LabelPlot.rearrange();
	}

	rearrange() {
		this.heatMap.rearrange();
		this.dotPlot.rearrange();
	}
}

