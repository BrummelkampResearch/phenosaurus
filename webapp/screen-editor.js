import 'bootstrap/js/dist/modal';

export default class ScreenEditor {

	constructor(action) {
		this.action = action;

		this.dialog = document.getElementById("screen-dialog");
		this.form = document.getElementById("screen-edit-form");
		this.csrf = this.form.elements['_csrf'].value;

		this.form.addEventListener("submit", (evt) => this.saveScreen(evt));

		const submit = document.getElementById("submit-btn");
		if (submit)
			submit.addEventListener("click", ev => this.saveScreen(ev));

		const btn = document.getElementById("upload-datafile-btn");
		if (btn != null)
			btn.addEventListener("click", (e) => this.uploadDataFile(e))
	}

	static init(url) {
		/* global context_name */

		window.addEventListener("load", () => {

			const editor = new ScreenEditor(`${context_name}/${url}`);

			Array.from(document.getElementsByClassName("edit-screen-btn"))
				.forEach(btn => btn.addEventListener("click", () => editor.editScreen(btn.dataset.id)));

			Array.from(document.getElementsByClassName("delete-screen-btn"))
				.forEach(btn => btn.addEventListener("click", () => {
					return editor.deleteScreen(btn.dataset.id, btn.dataset.name);
				}));

			document.getElementById("add-screen-btn")
				.addEventListener("click", () => editor.createScreen());
		});
	}

	editScreen(id) {
		this.id = id;

		fetch(`${this.action}/${id}`, {credentials: "include", method: "get"})
			.then(async response => {
				if (response.ok)
					return response.json();

				const error = await response.json();
				console.log(error);
				throw error.error;
			})
			.then(data => {

				this.screen = data;

				// this.form.reset();

				this.form.elements["name"].value = this.screen.name;
				this.form.elements["screen-type"].value = this.screen.type;
				document.getElementById("description").value = this.screen.description;
				this.form.elements["induced"].checked = this.screen.induced;
				this.form.elements["knockout"].checked = this.screen.knockout;
				this.form.elements["ignored"].checked = this.screen.ignored;
				this.form.elements["cell-line"].value = this.screen.cell_line;
				this.form.elements["genome"].value = this.screen.genome;
				this.form.elements["directory"].value = this.screen.directory;
				document.getElementById("long-description").value = this.screen.long_description;
				document.getElementById("sequence-ids").value = this.screen.sequenceIds;

				if (this.form.elements["scientist"] != null)
					this.form.elements["scientist"].value = this.screen.scientist;

				const g = new Set(this.screen.groups);
				[...this.form.getElementsByClassName("group-checkbox")].forEach(e => {
					e.checked = g.has(e.name);
				});

				// this.screen.groups.forEach(group => this.form.elements[group].checked = true);

				document.getElementById('data-file-block').style.display = 'none';

				$(this.dialog).modal();
			})
			.catch(err => alert(err));
	}

	saveScreen(e) {
		if (e)
			e.preventDefault();

		this.screen.name = this.form.elements["name"].value;
		this.screen.screenType = this.form.elements["screen-type"].value;
		this.screen.description = document.getElementById("description").value;
		this.screen.induced = this.form.elements["induced"].checked;
		this.screen.knockout = this.form.elements["knockout"].checked;
		this.screen.ignored = this.form.elements["ignored"].checked;
		this.screen.cellLine = this.form.elements["cell-line"].value;
		this.screen.genome = this.form.elements["genome"].value;
		this.screen.directory = this.form.elements["directory"].value;
		this.screen.longDescription = document.getElementById("long-description").value;
		this.screen.sequenceIds = document.getElementById("sequence-ids").value;
		if (this.form.elements["scientist"] != null)
			this.screen.scientist = this.form.elements["scientist"].value;
		this.screen.groups = Array.from(document.getElementsByClassName("group-checkbox"))
			.filter(input => input.checked)
			.map(input => input.name);

		const files = document
				.getElementById("uploadedFilesTbl")
			.tBodies[0]
			.getElementsByTagName("tr");
		this.screen.dataFileNrs = [...files].map(tr => tr.querySelector("td").textContent);

		const url = this.id ? `${this.action}/${this.id}` : this.action;
		const method = this.id ? 'put' : 'post';

		fetch(url, {
			credentials: "include",
			headers: {
				'Accept': 'application/json',
				'Content-Type': 'application/json',
				'X-CSRF-Token': this.csrf
			},
			method: method,
			body: JSON.stringify(this.screen)
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

	createScreen() {
		window.location = 'create-screen';
	}

	deleteScreen(id, name) {
		if (confirm(`Are you sure you want to delete screen ${name}?`)) {
			fetch(`${this.action}/${id}`, {
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

	uploadDataFile(e) {
		if (e) e.preventDefault();

		const screenName = this.form.elements["name"].value;
		if (screenName == null || screenName === "") {
			alert("Please enter the screen name first");
			return;
		}

		const file = this.form.elements['dataFile'].files[0];

		const formData = new FormData();
		formData.append("name", screenName);
		formData.append("genome", this.form.elements["genome"].value);
		formData.append("dataFile", file);
		formData.append("screen-type", this.form.elements["screen-type"].value);

		fetch(`${this.action}/data`, {
			credentials: "include",
			method: "post",
			headers: {
				'Accept': 'application/json',
				// 'Content-Type': 'multipart/form-data',
				// 'Content-Type': 'application/json',
				'X-CSRF-Token': this.csrf
			},
			body: formData
		}).then(async response => {
			if (response.ok)
				return response.json();

			const error = await response.json();
			console.log(error);
			throw error.error;
		}).then(data => {
			console.log(data);

			// document.getElementById("data-count").textContent = data.count;
			// document.getElementById("count-alert").style.display = 'unset';

			const tbl = document.getElementById("uploadedFilesTbl");
			const tr = document.createElement("tr");
			tbl.tBodies[0].appendChild(tr);

			let td = document.createElement("td");
			td.appendChild(document.createTextNode(data.fileId));
			tr.appendChild(td);

			td = document.createElement("td");
			td.appendChild(document.createTextNode(file.name));
			tr.appendChild(td);

			td = document.createElement("td");
			td.appendChild(document.createTextNode(data.count));
			tr.appendChild(td);

			td = document.createElement("td");
			let i = document.createElement("i");
			i.classList.add("fa", "fa-trash");
			td.appendChild(i);
			tr.appendChild(td);
			td.addEventListener("click", ev => {
				if (ev) ev.preventDefault();
				tbl.tBodies[0].removeChild(tr);
			});

			tbl.style.display = 'unset';

		}).catch(err => alert(err));

		return false;
	}
}
