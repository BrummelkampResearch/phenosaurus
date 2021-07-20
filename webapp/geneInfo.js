import * as d3 from "d3";

export let gene;

export default class GeneInfo {
	constructor() {
		this.div = document.getElementById("gene-info-dialog");
		this.header = this.div.getElementsByClassName("gene-info-dialog-header")[0];

		this.closeDragHandler = e => this.closeDragElement(e);
		this.elementDragHandler = e => this.elementDrag(e);

		this.header.addEventListener("mousedown", e => this.dragMouseDown(e));

		const btn = document.getElementById("close-info-dialog-button");
		btn.addEventListener("click", () => this.hide());

		this.pos = [{x: 0, y: 0}, {x: 0, y: 0}];

		this.showBtn = document.getElementById("show-gene-info");
		if (this.showBtn != null)
			this.showBtn.addEventListener("click", () => this.show());
	}

	dragMouseDown(e) {
		e = e || window.event;
		e.preventDefault();

		this.pos[1].x = e.clientX;
		this.pos[1].y = e.clientY;

		document.addEventListener("mouseup", this.closeDragHandler);
		document.addEventListener("mousemove", this.elementDragHandler);
	}

	elementDrag(e) {
		e = e || window.event;
		e.preventDefault();

		this.pos[0].x = this.pos[1].x - e.clientX;
		this.pos[0].y = this.pos[1].y - e.clientY;

		this.pos[1].x = e.clientX;
		this.pos[1].y = e.clientY;

		this.div.style.top = (this.div.offsetTop - this.pos[0].y) + "px";
		this.div.style.left = (this.div.offsetLeft - this.pos[0].x) + "px";
	}

	closeDragElement(e) {
		document.removeEventListener("mouseup", this.closeDragHandler);
		document.removeEventListener("mousemove", this.elementDragHandler);
	}

	show() {
		this.showBtn.style.display = 'none';
		this.div.style.display = '';
	}

	hide() {
		this.div.style.display = 'none';
		this.showBtn.style.display = '';
	}

	set(data) {
		const table = this.div.getElementsByClassName("table")[0];

		$("tr", table.tBodies[0]).remove();
		data.forEach(d => {
			const row = $("<tr/>");
			$("<td/>").text(d.gene).appendTo(row);
			$("<td/>").text(d3.format(".2e")(d.fcpv)).appendTo(row);
			$("<td/>").text(d.high).appendTo(row);
			$("<td/>").text(d.low).appendTo(row);
			row.appendTo(table);
		});
	}

	setSL(data) {
		const table = this.div.getElementsByClassName("table")[0];

		$("tr", table.tBodies[0]).remove();
		data.forEach(d => {
			const row = $("<tr/>");
			$("<td/>").text(d.gene).appendTo(row);
			$("<td/>").text(d3.format(".2e")(d.binom_fdr)).appendTo(row);
			$("<td/>").text(d.sense).appendTo(row);
			$("<td/>").text(d.antisense).appendTo(row);
			$("<td/>").text(d3.format(".2e")(d.odds_ratio)).appendTo(row);
			row.appendTo(table);
		});
	}
}

window.addEventListener("load", () => {
	gene = new GeneInfo();

});
