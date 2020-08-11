import * as d3 from 'd3';
import { highlightedGenes, radius, neutral } from './screenPlot';
import {gene} from "./geneInfo";

// --------------------------------------------------------------------

export default class MultiDot {
	constructor(circle, data, plot, screenNr) {
		this.g = circle.parentNode;
		this.circle = circle;
		this.data = data;
		this.plot = plot;
		this.screenNr = screenNr;

		const padding = 3;

		const N = data.values.length;
		const A = (Math.PI * 2) / N;
		const R = ((2 * radius + padding) / 2) / Math.sin(Math.PI - (A / 2));

		this.dR = R;

		const subData = data.values
			.map(g => {
				const ix = data.values.indexOf(g);
				const h = A * ix - Math.PI / 2;

				const ccx = Math.cos(h) * R;
				const ccy = Math.sin(h) * R;

				return {
					gene: g,
					values: [g],
					ix: ix,
					cx: ccx,
					cy: ccy,

					subdot: true,
					tx: ccx < 0 ? ccx - radius : ccx + radius,
					ty: ccy,
					anchor: ccx < 0 ? "end" : "begin",

					highlight: () => highlightedGenes.has(g.geneName),
					label: () => g.geneName
				};
			});

		d3.select(circle)
			.raise()
			.transition()
			.duration(500)
			.attr("r", (R + radius + padding))
			.style("stroke", "#ddd")
			.style("stroke-width", 1)
			.style("fill", "white")
			.style("opacity", 1);

		const dots = d3.select(this.g)
			.raise()
			.selectAll("g.dot")
			.data(subData, d => d.gene.key);

		dots.exit()
			.remove();

		let gs = dots.enter()
			.append("g")
			.attr("class", "dot");

		gs.append("circle")
			.classed("sub-dot", true)
			.attr("cx", 0)
			.attr("cy", 0)
			.attr("r", radius)
			.style("fill", this.getColor())
			.on("mouseover", d => gene.set(d.values))
			.on("mouseout", () => gene.set([]))
			.on("click", d => plot.clickGenes(d, screenNr))
			.on("dblclick", d => plot.dblClickGenes(d, screenNr))
			.transition()
			.duration(500)
			.attr("cx", d => d.cx)
			.attr("cy", d => d.cy);

			gs.filter(d => d.highlight())
			.append("text")
			.attr("class", "label")
			.attr("x", d => d.tx)
			.attr("y", d => d.ty)
			.attr("class", "label sub-dot")
			.attr("text-anchor", d => d.anchor)
			.text(d => d.gene.geneName)
			.style("opacity", 0)
			.transition()
			.delay(500)
			.style("opacity", 1);
	}

	hide() {
		d3.select(this.g)
			.selectAll("g.dot")
			.remove();

		d3.select(this.circle)
			.transition()
			.duration(250)
			.style("stroke-width", 0)
			.style("fill", neutral)
			.style("opacity", this.plot.getOpacity(this.screenNr))
			.attr("r", radius)
			.on("end", (d, ix, c) => {
				d3.select(c[ix])
					.style("fill", this.plot.getColor());
			});

		d3.select(this.g)
			.select("text")
			.text("")
			.transition()
			.delay(500)
			.text(d => d.label());
	}

	getColor() {
		const plotGetColor = this.plot.getColor();
		const v = this.data.values;

		return (d) => {
			return plotGetColor({
				values: [v[d.ix]]
			});
		};
	}
}