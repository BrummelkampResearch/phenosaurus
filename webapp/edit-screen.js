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

export class ScreenEditor {

	constructor() {
		this.form = document.forms['edit-screen-form'];
		this.csrf = this.form.elements['_csrf'].value;

		const btns = document.querySelectorAll(".btn-map-screen");
		btns.forEach(btn => {
			btn.addEventListener("click", () => this.mapScreen(btn.dataset.assembly, btn));
		});

		this.form.addEventListener('submit', (e) => this.submitForm(e));
	}

	async submitForm(e) {
		if (e) e.preventDefault();

		try {
			const screen = {
				name: this.form['screen-name'].value,
				published_name: this.form['screen-published-name'].value,
				// scientist: this.form['scientist'].value,
				// type: this.form['screen-type'].value,
				// detected_signal: this.form['detected-signal'].value,
				// genotype: this.form['genotype'].value,
				// treatment: this.form['treatment'].value,
				treatment_details: this.form['treatment-details'].value,
				cell_line: this.form['cell-line-clone'].value,
				description: this.form['description'].value,
				ignore: this.form['ignore'].checked,
				groups: []
				// files: []
			};

			[...document.querySelectorAll('#selected-groups input')]
				.filter(g => g.checked)
				.map(g => g.name)
				.forEach(g => screen.groups.push(g));

			const r = await fetch(`screen/${this.form['screen-name'].value}`, {
				body: JSON.stringify(screen),
				credentials: "include",
				method: 'PUT',
				headers: {
					'X-CSRF-Token': this.csrf,
					'Content-Type': 'application/json'
				}
			});

			const d = await r.json();

			if (typeof d.error === "string") {
				throw d.error;
			}

			if (r.ok)
				window.location = 'screens';
			else
				throw (await r).statusText;
		} catch (error) {
			alert(`Failed to edit screen: ${error}`);
		}
	}

	async mapScreen(assembly, btn) {
		try {
			const r = await fetch(`screen/${this.form['screen-name'].value}/map/${assembly}`, {
				credentials: "include",
				method: 'GET',
				headers: {
					'X-CSRF-Token': this.csrf
				}
			});

			if (r.ok) {
				btn.style.display = 'none';
				btn.parentNode.textContent = 'mapping started';
			}
			else {
				throw r.statusText;
			}
		} catch (error) {
			alert(`Failed to map screen: ${error}`);
		}
	}
}

window.addEventListener("load", () => {
	new ScreenEditor();
});
