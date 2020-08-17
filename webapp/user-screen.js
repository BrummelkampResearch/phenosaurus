import ScreenEditor from './screen-editor';

/* global context_name */

window.addEventListener("load", () => {

	const editor = new ScreenEditor(`${context_name}/screen`);

	Array.from(document.getElementsByClassName("edit-screen-btn"))
		.forEach(btn => btn.addEventListener("click", () => editor.editScreen(btn.dataset.id)));

	Array.from(document.getElementsByClassName("delete-screen-btn"))
		.forEach(btn => btn.addEventListener("click", () => {
			return editor.deleteScreen(btn.dataset.id, btn.dataset.name);
		}));

	document.getElementById("add-screen-btn")
		.addEventListener("click", () => editor.createScreen());
});
