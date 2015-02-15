#include "ofApp.h"

#include "ofXAudioSoundPlayer.h"

	ofXAudioSoundPlayer * player;
//--------------------------------------------------------------
void ofApp::setup(){
	/*string p = ofToDataPath("../../../../_QUIN/0280.wav", true);
	test.loadSound(p, true);
	cout << p << endl;
	test.play();*/
	player = new ofXAudioSoundPlayer();
	player->loadSound(ofToDataPath("F:/0280.wav", true), true);
}

//--------------------------------------------------------------
void ofApp::update(){
}

//--------------------------------------------------------------
void ofApp::draw(){
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){

}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
