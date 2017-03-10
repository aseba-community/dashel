#!groovy

// Jenkinsfile for compiling, testing, and packaging the Dashel libraries.
// Requires CMake plugin from https://github.com/davidjsherman/aseba-jenkins.git in global library.

pipeline {
	agent any // use any available Jenkins agent
	stages {
		stage('Prepare') {
			steps {
				dir('dashel') {
					checkout scm
				}
				stash includes: 'dashel/**', excludes: '.git', name: 'source'
			}
		}
		stage('Compile') {
			steps {
				parallel (
					"debian" : {
						node('debian') {
							unstash 'source'
							CMake([sourceDir: pwd()+'/dashel', label: 'debian'])
							stash includes: 'dist/**', name: 'dist-debian'
							stash includes: 'build/**', name: 'build-debian'
						}
					},
					"macos" : {
						node('macos') {
							unstash 'source'
							CMake([sourceDir: pwd()+'/dashel', label: 'macos'])
							stash includes: 'dist/**', name: 'dist-macos'
						}
					},
					"windows" : {
						node('windows') {
							unstash 'source'
							CMake([sourceDir: pwd()+'/dashel', label: 'windows'])
							stash includes: 'dist/**', name: 'dist-windows'
						}
					}
				)
			}
		}
		stage('Test') {
			steps {
				node('debian') {
					unstash 'build-debian'
					dir('build/debian') {
						sh 'LANG=C ctest'
					}
				}
			}
		}
		stage('Package') {
			steps {
				parallel (
					"debian" : {
						node('debian') {
							unstash 'dist-debian'
							unstash 'source'
							dir('dashel') {
								sh 'which debuild && debuild -i -us -uc -b'
							}
							sh 'mv libdashel*.deb libdashel*.changes libdashel*.build dist/debian/'
							stash includes: 'dist/**', name: 'dist-debian'
						}
					}
				)
			}
		}
		stage('Archive') {
			steps {
				script {
					// Can't use collectEntries yet [JENKINS-26481]
					def p = [:]
					for (x in ['debian','macos','windows']) {
						def label = x
						p[label] = {
							node(label) {
								unstash 'dist-' + label
								archiveArtifacts artifacts: 'dist/**', fingerprint: true, onlyIfSuccessful: true
							}
						}
					}
					parallel p;
				}
			}
		}
	}
}
