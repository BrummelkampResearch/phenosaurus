/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2022 NKI/AVL, Netherlands Cancer Institute
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

import * as d3 from "d3";
import { format_pv } from "./pvformat";

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
			$("<td/>").text(format_pv(d.fcpv)).appendTo(row);
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
