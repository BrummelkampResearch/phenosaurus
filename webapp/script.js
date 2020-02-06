export function readMyFile(file) {
    return new Promise((resolve, reject) => {
        const fr = new FileReader();
        fr.onload = e => resolve(e.target.result);
        fr.readAsText(file);
    })
}
//
//
// $(function () {
//     // Safari 3.0+ "[object HTMLElementConstructor]"
//     var isSafari = /constructor/i.test(window.HTMLElement) || (function (p) {
//         return p.toString() === "[object SafariRemoteNotification]";
//     })(!window['safari'] || (typeof safari !== 'undefined' && safari.pushNotification));
//
//     if (isSafari && sessionStorage.getItem("safari-warning") == null) {
//         alert("You're using Safari, not everything will work as intended.");
//         sessionStorage.setItem("safari-warning", 1);
//     }
// });
