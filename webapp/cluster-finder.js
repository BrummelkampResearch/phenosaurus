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

import * as d3 from 'd3';
import $ from 'jquery';

import { geneSelectionEditor } from './gene-selection';
import { GeneLine } from './gene-finder';

class ClusterLine {
	constructor(cl, nr) {
		this.data = cl;

		const template = $("#cluster-tabel > tbody:first > tr:first");
		const line = template.clone(true);
		
		this.line = line;
		this.row = line[0];
		// this.data = [];

		const maxGenes = 30;

		this.row.cells[0].innerText = nr;
		this.row.cells[1].innerText = cl.variance;

		if (cl.genes.length <= maxGenes)
			this.row.cells[2].innerText = cl.genes.join(", ");
		else
			this.row.cells[2].innerText = `${cl.genes.slice(0, maxGenes).join(", ")} and ${cl.genes.length - maxGenes} more`;
		
		line.prop("clusterLine", this)
			.appendTo(template.parent())
			.show();

		this.row.addEventListener("click", () => this.clicked());
		this.row.addEventListener("dblclick", () => this.dblclicked());
	}

	clicked() {
	}

	dblclicked() {
		$("#displayClusterModal").modal('show');

		$("#plot > tbody:first > tr:not(:first-child)").remove();
		// $("#plot > tbody:last > tr").remove();
		document.getElementById("plot").classList.add("no-anti");
	
		// if (this.data.findIndex((v) => v.anti) >= 0)
		// 	document.getElementById("plot").classList.remove("no-anti");

		this.data.genes.forEach(d => new GeneLine(d));
	}
}

function doCluster() {

	let plotTitle = $(".plot-title");
	if (plotTitle.hasClass("plot-status-loading"))  // avoid multiple runs
		return;

	$("#cluster-tabel > tbody > tr:not(:first-child)").remove();

	plotTitle.addClass("plot-status-loading")
		.removeClass("plot-status-loaded")
		.removeClass("plot-status-failed")
		.removeClass("plot-status-no-hits");

	const options = geneSelectionEditor.getOptions();

	options.append("pv-cutoff", document.getElementById("pv-cut-off").value);
	options.append("eps", document.getElementById("eps").value);
	// fd.append("dmax", document.getElementById("dmax").value);
	options.append("nns", document.getElementById("nns").value);
	options.append("minPts", document.getElementById("minPts").value);

	const uri = 'clusters';

	fetch(uri, {
		body: options,
		method: 'POST',
		credentials: "include"
	})
	.then(response => {
		if (response.ok)
			return response.json();

		response.json()
			.then(err => { throw err.error; })
			.catch(err => { throw err; });
	})
	.then(data => {
		let nr = 1;
		data.forEach(d => {
			new ClusterLine(d, ++nr);
		});

		console.log(data);

		plotTitle.removeClass("plot-status-loading")
			.toggleClass("plot-status-loaded", data.length > 0)
			.toggleClass("plot-status-no-hits", data.length === 0);
	})
	.catch(err => {
		console.log(err);
		plotTitle.removeClass("plot-status-loading").addClass("plot-status-failed");
	});
}

window.addEventListener('load', () => {
	document.getElementById("cluster-btn").addEventListener("click", doCluster);
});