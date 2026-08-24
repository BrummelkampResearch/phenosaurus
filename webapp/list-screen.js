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

import * as bootstrap from 'bootstrap';

function attachEventListeners() {
	[...document.getElementsByClassName('edit-screen-btn')]
		.forEach(btn => {
			btn.addEventListener('click', e => {
				if (e) e.preventDefault();

				const screenID = btn.dataset["id"];
				window.location = `edit-screen?screen-id=${screenID}`;
			})
		});	

		[...document.getElementsByClassName('delete-screen-btn')]
		.forEach(btn => {
			btn.addEventListener('click', e => {
				if (e) e.preventDefault();

				const screenID = btn.dataset["id"];
				if (confirm(`Are you sure you want to delete screen ${screenID}`)) {
					fetch(`screen/${screenID}`, {
						method: 'DELETE',
						credentials: 'include'
					}).then(r => {
						return r.json();
					}).then(r => {
						if (r && typeof r.error === 'string')
							throw r.error;
						window.location.reload();
					}).catch(err => {
						console.log(err);
						alert(err);
					})
				}
			})
		});
}

window.addEventListener("load", () => {
	const createBtn = document.getElementById('add-screen-btn');
	if (createBtn)
		createBtn.addEventListener('click', () => {
			window.location = 'create-screen';
		});
	
	attachEventListeners();

	const parent = document.getElementById("screen-table-container");

	// refresh the list every 30 seconds
	setInterval(async () => {
		try {
			const reply = await fetch('screen-table', { credentials: 'include' });
			if (reply.ok) {
				const t = await reply.text();
	
				const container = document.createElement('div');
				container.innerHTML = t || "";
	
				parent.replaceChildren(container);
				attachEventListeners();
			}
		} catch (error) {
			console.log(error);
		}
	}, 15000);
});
