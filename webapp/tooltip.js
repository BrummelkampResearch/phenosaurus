import * as d3 from "d3";

export default class Tooltip {

	constructor() {
		this.div = null;
	}

	show(msg, x, y) {
		if (this.div == null) {
			this.div = d3.select("body").append("div")
				.attr("class", "tooltip")
				.style("opacity", 0);
		}

		this.div.transition()
			.duration(200)
			.style("opacity", .9);
		this.div.html(msg)
			.style("left", x + "px")
			.style("top", y + "px");
	}

	hide() {
		if (this.div != null) {
			this.div.transition()
				.duration(500)
				.style("opacity", 0);
		}
	}
}
