#!groovy

// Jenkinsfile for compiling, testing, and packaging the Dashel libraries.
// Requires CMake plugin from https://github.com/davidjsherman/aseba-jenkins.git in global library.

pipeline {
	agent label:'' // use any available Jenkins agent
	stages {
		stage('Prepare') {
			steps {
				sh 'mkdir -p build dist'
				dir('dashel') {
					checkout scm
				}
				stash includes: 'dashel/**', excludes: '.git', name: 'source'
			}
		}
		stage('Compile') {
			steps {
				unstash 'source'
				CMake([buildType: 'Debug',
					   sourceDir: '$workDir/dashel',
					   buildDir: '$workDir/build/dashel',
					   installDir: '$workDir/dist',
					   getCmakeArgs: [ '-DBUILD_SHARED_LIBS:BOOL=ON' ]
					  ])
			}
			post {
				always {
					stash includes: 'dist/**', name: 'dashel'
				}
			}
		}
		stage('Test') {
			steps {
				dir('build/dashel') {
					sh 'ctest'
				}
			}
		}
		stage('Package') {
			when {
				sh(script:'which debuild', returnStatus: true) == 0
			}
			steps {
				unstash 'source'
				sh 'cd dashel && debuild -i -us -uc -b'
				sh 'mv libdashel*.deb libdashel*.changes libdashel*.build dist/'
			}
		}
		stage('Archive') {
			steps {
				archiveArtifacts artifacts: 'dist/**', fingerprint: true, onlyIfSuccessful: true
			}
		}
	}
}
