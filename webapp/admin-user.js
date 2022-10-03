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

import 'bootstrap/js/dist/modal';

/*global context_name */

class UserEditor {

	constructor() {
		this.dialog = document.getElementById("user-dialog");
		this.form = document.getElementById("user-edit-form");
		this.csrf = this.form.elements['_csrf'].value;

		this.form.addEventListener("submit", (evt) => this.saveUser(evt));

		const showPassword = this.form.querySelector(".show-password-btn");
		if (showPassword)
			showPassword.addEventListener("click", () => this.toggleShowPassword());
	}

	editUser(id) {
		this.id = id;

		$(this.dialog).modal();

		document.getElementById("id_username").focus();

		fetch(`${context_name}/admin/user/${id}`, {credentials: "include", method: "get"})
			.then(async response => {
				if (response.ok)
					return response.json();

				const error = await response.json();
				console.log(error);
				throw error.error;
			})
			.then(data => {

				this.user = data;

				for (let key in data) {
					const input = this.form.elements[key];
					if (input == null)
						continue;

					switch (input.type) {
						case 'checkbox':
							input.checked = !!data[key];
							break;
						case 'password':
							input.value = '';
							break;
						default:
							input.value = data[key];
							break;
					}
				}
			})
			.catch(err => alert(err));
	}

	saveUser(e) {
		if (e)
			e.preventDefault();

		for (let key in this.user) {
			const input = this.form.elements[key];
			if (input == null)
				continue;

			switch (input.type) {
				case 'checkbox':
					this.user[key] = input.checked;
					break;
				case 'password':
					if (input.value !== '')
						this.user[key] = input.value;
					break;
				default:
					this.user[key] = input.value;
					break;
			}
		}

		this.user.id = +this.user.id;

		const url = this.id ? `${context_name}/admin/user/${this.id}` : `${context_name}/admin/user`;
		const method = this.id ? 'put' : 'post';

		fetch(url, {
			credentials: "include",
			headers: {
				'Accept': 'application/json',
				'Content-Type': 'application/json',
				'X-CSRF-Token': this.csrf
			},
			method: method,
			body: JSON.stringify(this.user)
		}).then(async response => {
			if (response.ok)
				return response.json();

			const error = await response.json();
			console.log(error);
			throw error.error;
		}).then(r => {
			console.log(r);
			$(this.dialog).modal('hide');

			window.location.reload();
		}).catch(err => alert(err));
	}

	toggleShowPassword() {
		const pwField = this.form.elements['password'];
		if (pwField.type === 'password')
			pwField.type = 'text';
		else
			pwField.type = 'password';
	}

	createUser() {
		this.id = null;
		this.user = {};

		Array.from(this.form.elements)
			.filter(i => i.tagName === 'INPUT')
			.forEach(input => this.user[input.name] = '');

		this.form.reset();
		$(this.dialog).modal();

		document.getElementById("id_username").focus();
	}

	deleteUser(id, name) {
		if (confirm(`Are you sure you want to delete user ${name}?`)) {
			fetch(`${context_name}/admin/user/${id}`, {
				credentials: "include",
				method: "delete",
				headers: {
					'Accept': 'application/json',
					// 'Content-Type': 'application/json',
					'X-CSRF-Token': this.csrf
				}
			}).then(async response => {
				if (response.ok)
					return response.json();

				const error = await response.json();
				console.log(error);
				throw error.error;
			}).then(data => {
				console.log(data);

				window.location.reload();
			})
			.catch(err => alert(err));
		}
	}
}

window.addEventListener("load", () => {

	const editor = new UserEditor();

	Array.from(document.getElementsByClassName("edit-user-btn"))
		.forEach(btn => btn.addEventListener("click", () => editor.editUser(btn.dataset.id)));

	Array.from(document.getElementsByClassName("delete-user-btn"))
		.forEach(btn => btn.addEventListener("click", () => {
			return editor.deleteUser(btn.dataset.id, btn.dataset.name);
		}));

	document.getElementById("add-user-btn")
		.addEventListener("click", () => editor.createUser());


	// Array.from(document.getElementById('user-table').tBodies[0].rows)
	// 	.forEach(tr => {
	// 		tr.addEventListener("dblclick", () => {
	// 			editor.editUser(tr.dataset.uid);
	// 		})
	// 	});
});
