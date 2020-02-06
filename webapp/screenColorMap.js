import * as d3 from "d3";

export default class ScreenColorMap {

	constructor() {
		this.screenNumbers = new Set();
	}

	add(screenNr) {
		this.screenNumbers.add(screenNr);
		this.colorMap = d3.scaleOrdinal(d3.schemeCategory10)
			.domain([...this.screenNumbers]);
		return this.colorMap(screenNr);
	}

	get(screenNr) {
		return this.colorMap(screenNr);
	}
}