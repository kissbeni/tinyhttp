
const socket = new WebSocket("ws://localhost:8088/ws");

socket.addEventListener("open", function (event) {
    //socket.send('Hello Server!');
});

socket.addEventListener("message", function (event) {
    let data = JSON.parse(event.data);
    console.log(data);

    if (data.type === "text_message") {
        addMessageToContainer(data.sender.name, data.sender.id, "todo", data.content);
    }
});

const message_container = document.getElementById("messages");
let prevId = null;

function addMessageToContainer(senderName, senderId, timeString, content) {
    let div = document.createElement("DIV");
    div.className = `message ${senderId === "$self" ? "self" : ""}`;

    if (prevId !== senderId) {
        let senderNameAndTime = document.createElement("SMALL");
        senderNameAndTime.innerText = `${senderName} ~ ${timeString}`;
        div.appendChild(senderNameAndTime);
    }

    prevId = senderId;

    let messageContent = document.createElement("P");
    messageContent.innerText = content;
    div.appendChild(messageContent);

    message_container.appendChild(div);
}

function sendMessage(body) {
    socket.send(JSON.stringify({
        message: body
    }));
}
