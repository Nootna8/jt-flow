import Video from './Video.vue'
import { createApp } from 'vue'

window.hostType = null;

async function load() {
    let element = document.createElement('div');
    element.id = 'jt-video';
    
    let videoWrapper = document.querySelector(".play_cover");
    if(videoWrapper) {
        window.hostType = "spang";
        videoWrapper.parentElement.appendChild(element);
        createApp(Video).mount('#jt-video')
        return
    }
    
    videoWrapper = document.querySelector("#player");
    if(videoWrapper) {
        window.hostType = "ph";

        videoWrapper.parentElement.insertBefore(element, document.querySelector('.video-actions-container'));
        createApp(Video).mount('#jt-video')
        return
    }
}

load();


document.querySelector(".play_cover")



// import cv from "@techstark/opencv-js";

// let videoId = null;
// let port = null;
// let element = null;

// function findSrc() {
//     var video = document.querySelector("video.vjs-tech");
//     var src = null
//     if(video) {
//         src = video.src;
//     }

//     src = src.replace('://', '/')

//     return src
// }

// function injectElement() {
//     element = document.querySelector(".play_cover").appe
// }

// async function main() {
//     const src = findSrc();
    
//     if(!src) {
//         console.log("No video found");
//         return
//     }
//     injectElement();

//     console.log("Video script loaded!", src);
//     const response = await chrome.runtime.sendMessage({
//         type: "flow",
//         path: {
//             path: src,
//             srcPath: window.location.href
//         }
//     });
//     console.log("Response: ", response);
//     if(!response.videoId) {
//         return;
//     }
//     videoId = response.videoId;
    
//     port = chrome.runtime.connect({name: response.videoId});
//     port.onMessage.addListener(function(msg) {
//         console.log("Video data recieved" , msg);
//     });
   
    
// }

// main();