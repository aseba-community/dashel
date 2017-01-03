#!groovy

// Jenkinsfile for compiling, testing, and packaging the Dashel libraries.
// Requires CMake plugin from https://github.com/davidjsherman/aseba-jenkins.git in global library.

pipeline {
	agent any // use any available Jenkins agent
	stages {
		stage('Prepare') {
			steps {
				checkout scm
			}
		}
		stage('Compile') {
			parallel {
				stage("Compile on debian") {
					agent {
						label 'debian'
					}
					steps {
						CMake([label: 'debian', getCmakeArgs: '-DBUILD_SHARED_LIBS:BOOL=OFF'])
						stash includes: 'dist/**', name: 'dist-debian'
						stash includes: 'build/**', name: 'build-debian'
					}
				}
				stage("Compile on macos") {
					agent {
						label 'macos'
					}
					steps {
						CMake([label: 'macos', getCmakeArgs: '-DBUILD_SHARED_LIBS:BOOL=OFF'])
						stash includes: 'dist/**', name: 'dist-macos'
					}
				}
				stage("Compile on windows") {
					agent {
						label 'windows'
					}
					steps {
						CMake([label: 'windows', getCmakeArgs: '-DBUILD_SHARED_LIBS:BOOL=OFF'])
						stash includes: 'dist/**', name: 'dist-windows'
					}
				}
			}
		}
		stage('Test') {
			parallel {
				stage("Test on debian") {
					agent {
						label 'debian'
					}
					steps {
						unstash 'build-debian'
						dir('build/debian') {
							sh 'LANG=C ctest'
						}
					}
				}
			}
		}
		stage('Package') {
			parallel {
				stage("Build debian package") {
					agent {
						label 'debian'
					}
					steps {
						dir('build/debian/package') {
							// We must rebuild in a subdirectory to prevent debuild from polluting the workspace parent
							sh 'git clone --depth 1 --single-branch $GIT_URL'
							sh '(cd dashel && which debuild && debuild -i -us -uc -b)'
							sh 'mv libdashel*.deb libdashel*.changes libdashel*.build $WORKSPACE/dist/debian/'
						}
						stash includes: 'dist/**', name: 'dist-debian'
					}
				}
			}
		}
		stage('Archive') {
			steps {
				unstash 'dist-debian'
				unstash 'dist-macos'
				unstash 'dist-windows'
				archiveArtifacts artifacts: 'dist/**', fingerprint: true, onlyIfSuccessful: true
			}
		}
	}
}
