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
import { fillTable } from "./table-utils";

class GeneInfo {
	constructor() {
		window.addEventListener("load", () => {
			try {
				this.init();
			} catch (error) {
				console.error(error);
			}
		})
	}

	init() {
		this.div = document.getElementById("gene-info-dialog");

		this.showBtn = document.getElementById("show-gene-info-btn");
		if (this.showBtn != null)
			this.showBtn.addEventListener("click", (evt) => {
				if (this.showBtn.classList.contains("active"))
					this.hide();
				else
					this.show();
			});
		
		this.div.style.display = "none";
	}

	show() {
		this.div.style.display = 'block';
		this.showBtn.classList.add("active");
	}

	hide() {
		this.div.style.display = 'none';
		this.showBtn.classList.remove("active");
	}

	set(data) {
		const table = this.div.querySelector("table");
		fillTable(table, data, d => [d.gene, format_pv(d.fcpv), d.high, d.low]);
	}

	setSL(data) {
		const table = this.div.querySelector("table");
		const fmt = d3.format(".2e");
		fillTable(table, data, d => [d.gene, fmt(d.binom_fdr), d.sense, d.antisense, fmt(d.odds_ratio)]);
	}
}

export const gene = new GeneInfo();

