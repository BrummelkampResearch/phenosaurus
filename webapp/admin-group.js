import 'bootstrap/js/dist/modal';

/*global context_name, $ */

class GroupEditor {

	constructor() {
		this.dialog = document.getElementById("group-dialog");
		this.form = document.getElementById("group-edit-form");
		this.csrf =
			this.form.elements['_csrf'].value || document.logoutForm.elements['_csrf'].value;

		this.form.addEventListener("submit", (evt) => this.saveGroup(evt));

		const showPassword = this.form.querySelector(".show-password-btn");
		if (showPassword)
			showPassword.addEventListener("click", () => this.toggleShowPassword());
	}

	editGroup(id) {
		this.id = id;

		$(this.dialog).modal();

		fetch(`${context_name}/admin/group/${id}`, {credentials: "include", method: "get"})
			.then(async response => {
				if (response.ok)
					return response.json();

				const error = await response.json();
				console.log(error);
				throw error.error;
			})
			.then(data => {

				this.group = data;

				const members = new Set(data.members);

				for (let key in this.form.elements) {
					const input = this.form.elements[key];
					switch (input.type) {
						case 'checkbox':
							input.checked = members.has(input.name);
							break;
						case 'text':
							input.value = data[input.name];
							break;
					}
				}
			})
			.catch(err => alert(err));
	}

	saveGroup(e) {
		if (e)
			e.preventDefault();

		const members = new Set();
		for (let key in this.form.elements) {
			const input = this.form.elements[key];
			if (input == null || input.type !== 'checkbox' || !input.checked)
				continue;

			members.add(input.name);
		}

		this.group = {
			id: +this.group.id
		};
		this.group.name = this.form.elements['name'].value;
		this.group.members = Array.from(members.values());

		const url = this.id ? `${context_name}/admin/group/${this.id}` : `${context_name}/admin/group`;
		const method = this.id ? 'put' : 'post';

		fetch(url, {
			credentials: "include",
			headers: {
				'Accept': 'application/json',
				'Content-Type': 'application/json',
				'X-CSRF-Token': this.csrf
			},
			method: method,
			body: JSON.stringify(this.group)
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

	createGroup() {
		this.id = null;
		this.group = {};

		$(this.dialog).modal();

		Array.from(this.form.elements)
			.filter(i => i.tagName === 'INPUT')
			.forEach(input => {

				this.group[input.name] = '';

				switch (input.type) {
					case 'checkbox':
						input.checked = false;
						break;
					default:
						input.value = '';
						break;
				}
			});
	}

	deleteGroup(id, name) {
		if (confirm(`Are you sure you want to delete group ${name}?`)) {
			fetch(`${context_name}/admin/group/${id}`, {
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

	const editor = new GroupEditor();

	Array.from(document.getElementsByClassName("edit-group-btn"))
		.forEach(btn => btn.addEventListener("click", () => editor.editGroup(btn.dataset.id)));

	Array.from(document.getElementsByClassName("delete-group-btn"))
		.forEach(btn => btn.addEventListener("click", () => {
			return editor.deleteGroup(btn.dataset.id, btn.dataset.name);
		}));

	document.getElementById("add-group-btn")
		.addEventListener("click", () => editor.createGroup());
});
