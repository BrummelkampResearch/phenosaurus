// import '@gmod/jbrowse';



var features = [];
// Add some features
var config = {
	containerID: "GenomeBrowser",
	baseUrl: "./jbrowse/",
};

// Add to the config or tracks

// Instantiate JBrowse
window.addEventListener("load", () => {
	window.JBrowse = new window.Browser(config);
});