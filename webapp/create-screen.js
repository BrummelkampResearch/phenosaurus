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

class ScreenCreator {

	constructor() {
		this.form = document.forms['create-screen-form'];
		this.csrf = this.form.elements['_csrf'].value;

		this.form['screen-type'].addEventListener('input', () => this.updateFastQBox());
		this.form["detected-signal"].addEventListener("input", () => this.updateScreenName());
		this.form["genotype"].addEventListener("input", () => this.updateScreenName());
		this.form["treatment"].addEventListener("input", () => this.updateScreenName());

		for (let f of ['low', 'high', 'replicate-1', 'replicate-2', 'replicate-3', 'replicate-4']) {
			const fn = `fastq-${f}`;
			this.form[fn].addEventListener('change', () => this.validateFile(f));
		}

		this.updateFastQBox();
		this.updateScreenName();

		this.form.addEventListener('submit', (e) => this.submitForm(e));
	}

	updateScreenName() {
		const detectedSignal = this.form['detected-signal'].value;
		const genotype = this.form['genotype'].value;
		const treatment = this.form['treatment'].value;

		let screenName;

		if (this.form['screen-type'].value == 'sl') {
			if (typeof treatment == 'string' && treatment.length)
				screenName = `${genotype} (${treatment})`;
			else
				screenName = `${genotype}`;
		}
		else {
			if (typeof treatment == 'string' && treatment.length)
				screenName = `${detectedSignal}-in-${genotype} (${treatment})`;
			else
				screenName = `${detectedSignal}-in-${genotype}`;
		}

		this.form['screen-name'].value = screenName
			.replaceAll(/[ \n\r\t]/g, '-')
			.replaceAll(/[:<>|&]/g, '')
			.replaceAll(/--+/g, '-');

		this.form['treatment-details'].parentNode.classList.toggle('d-none', treatment.length == 0);
		this.form['treatment-details'].required = treatment.length > 0;
	}

	updateFastQBox() {
		const type = this.form['screen-type'].value;
		if (type == 'sl') {
			this.form['fastq-low'].required = false;
			this.form['fastq-high'].required = false;
	
			document.getElementById('fastq-sl').classList.remove('d-none');
			document.getElementById('fastq-ip').classList.add('d-none');
	
			this.form['detected-signal'].parentNode.classList.add('invisible');
			this.form['detected-signal'].required = false;
		}
		else {
			this.form['fastq-low'].required = true;
			this.form['fastq-high'].required = true;
	
			document.getElementById('fastq-ip').classList.remove('d-none');
			document.getElementById('fastq-sl').classList.add('d-none');
	
			this.form['detected-signal'].parentNode.classList.remove('invisible');
			this.form['detected-signal'].required = true;
		}
	}

	validateFile(f) {
		const input = this.form[`fastq-${f}`];

		const fd = new FormData();
		fd.append('file', input.value);

		let wasOK;
		fetch(`screen/validate/fastq?file=${input.value}`, {
			credentials: "include", method: 'POST', body: fd
		}).then(r => {
			wasOK = r.ok;
			return r.json();
		})
		.then(r => {
			if (wasOK)
				input.setCustomValidity('');
			else if (r.error)
				input.setCustomValidity(`Failed to validate FastQ file: ${r.error}`);
			else
			input.setCustomValidity("Failed to validate FastQ file, no error message");
		}).catch((e) => {
			alert(`Could not validate fastq file: ${e}`);
		})
	}

	validateScreenName() {
		return new Promise((resolve, reject) => {
			const screenName = this.form['screen-name'];
			const fd = new FormData();
			fd.append('name', screenName);
			
			fetch(`screen/validate/name?name=${screenName.value}`, {
				credentials: "include", method: 'POST', body: fd
			}).then(r => {
				if (r.ok == false)
					reject('Invalid response from server');
				else
					return r.json();
			}).then(r => {
				r === true ? resolve() : reject(`unexpected result from server: ${r}`);
			});
		});
	}

	submitForm(e) {
		if (e) e.preventDefault();

		this.validateScreenName()
			.then(() => {
		
				const screen = {
					name: this.form['screen-name'].value,
					published_name: (this.form['screen-published-name'] || this.form['screen-name']).value,
					scientist: this.form['scientist'].value,
					type: this.form['screen-type'].value,
					detected_signal: this.form['detected-signal'].value,
					genotype: this.form['genotype'].value,
					treatment: this.form['treatment'].value,
					treatment_details: this.form['treatment-details'].value,
					cell_line: this.form['cell-line-clone'].value,
					description: this.form['description'].value,
					ignore: this.form['ignore'].checked,
					files: []
				};

				if (screen.type == 'sl') {
					for (let r of [1, 2, 3, 4]) {
						const fn = `fastq-replicate-${r}`;
						const fv = this.form[fn].value;
						if (fv.length > 0)
							screen.files.push({ name: `replicate-${r}`, source: fv});
					}
				} else {
					for (let c of ['low', 'high']) {
						const fn = `fastq-${c}`;
						const fv = this.form[fn].value;
						if (fv.length > 0)
							screen.files.push({ name: c, source: fv});
					}
				}

				let wasOK;
				fetch(`screen`, {
					body: JSON.stringify(screen),
					credentials: "include",
					method: 'POST',
					headers: {
						'X-CSRF-Token': this.csrf,
						'Content-Type': 'application/json'
					}
				}).then(r => {
					wasOK = r.ok;
					return r.json();
				}).then(r => {
					if (r.error)
						throw r.error;
					if (wasOK == false)
						throw 'server returned an error';
					
					// this.form.reset();
					window.location = 'screens';
				}).catch(err => {
					console.log(err);
					alert(`Failed to submit form: ${err}`);
				});
			})
			.catch(err => {
				console.log(err);
				alert("The screen name is not valid, is it unique?");
			});
	}
}

window.addEventListener("load", () => {
	new ScreenCreator();
});