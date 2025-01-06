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

import "core-js/stable";
import "regenerator-runtime/runtime";

import { geneSelectionEditor } from './gene-selection';
import { GeneLine } from './gene-line';
import * as bootstrap from 'bootstrap';

const maxGenes = 30;

class ClusterLine {
	constructor(cl, nr) {
		this.data = cl;

		const template = document.querySelector("#cluster-row-template");
		const clone = template.cloneNode(true);
		const line = clone.content.querySelector("tr");
		
		this.line = line;

		const tds = [...line.querySelectorAll("td")];

		tds[0].innerText = nr;
		tds[1].innerText = cl.variance;

		if (cl.genes.length <= maxGenes)
			tds[2].innerText = cl.genes.join(", ");
		else
			tds[2].innerText = `${cl.genes.slice(0, maxGenes).join(", ")} and ${cl.genes.length - maxGenes} more`;
		
		line.clusterLine = this;
		document.querySelector("#cluster-tabel").appendChild(line);

		this.line.addEventListener("click", () => this.clicked());
		this.line.addEventListener("dblclick", () => this.dblclicked());
	}

	clicked() {
	}

	dblclicked() {
		const dlog = new bootstrap.Modal(document.querySelector("#displayClusterModal"));
		dlog.show();

		const plot = document.querySelector("#plot");
		plot.querySelector("tbody:first-of-type").replaceChildren();
		document.getElementById("plot").classList.add("no-anti");
		this.data.genes.slice(0, maxGenes).forEach(d => new GeneLine(d));
	}
}

async function doCluster() {

	let plotTitle = document.querySelector(".plot-title");
	if (plotTitle.classList.contains("plot-status-loading"))  // avoid multiple runs
		return;

	plotTitle.style.display = '';
	plotTitle.classList.add("plot-status-loading");
	plotTitle.classList.remove("plot-status-loaded");
	plotTitle.classList.remove("plot-status-failed");
	plotTitle.classList.remove("plot-status-no-hits");

	const options = geneSelectionEditor.getOptions();

	options.append("pv-cutoff", document.getElementById("pv-cut-off").value);
	options.append("eps", document.getElementById("eps").value);
	// fd.append("dmax", document.getElementById("dmax").value);
	options.append("nns", document.getElementById("nns").value);
	options.append("minPts", document.getElementById("minPts").value);

	try {
		const resp = await fetch("clusters", {
			body: options,
			method: 'POST',
			credentials: "include"
		});

		if (resp.ok == false) {
			const err = await resp.json();
			throw err.error;
		}

		const data = await resp.json();

		let nr = 1;
		data.forEach(d => new ClusterLine(d, ++nr));

		plotTitle.classList.remove("plot-status-loading")
		plotTitle.classList.toggle("plot-status-loaded", data.length > 0);
		plotTitle.classList.toggle("plot-status-no-hits", data.length === 0);
	} catch (err) {
		console.log(err);
		plotTitle.classList.remove("plot-status-loading")
		plotTitle.classList.add("plot-status-failed");;
	};
}

window.addEventListener('load', () => {
	document.getElementById("cluster-btn").addEventListener("click", doCluster);
});