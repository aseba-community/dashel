#!groovy

def CMake = fileLoader.fromGit('vars/CMake', 'https://github.com/davidjsherman/aseba-jenkins.git', 'master', null, '')

stage 'Setup'
node() {
  git branch: 'pollsocketstream', url: 'https://github.com/davidjsherman/dashel.git'
  stash excludes: '.git', name: 'source'
}

def labels = ['inirobot-deb64', 'inirobot-u64', 'inirobot-win7', 'inirobot-osx']
def builders = [:]		// map to fire all builds in parallel
for (x in labels) {
  def label = x			// Need to bind the label variable before the closure
  def output = "dashel-" + x
  
  builders[label] = {
    node(label) {
      unstash 'source'
      CMake.call([buildType: 'Debug',
		  buildDir: '$workDir/_build',
		  installDir: '$workDir/_install/' + output,
		  getCmakeArgs: '-DBUILD_SHARED_LIBS:BOOL=OFF'
		 ])
      stash includes: '_install/'+output+'/**', name: output
      archiveArtifacts artifacts: '_install/'+output+'/**', fingerprint: true, onlyIfSuccessful: true
    }
  }
}

stage 'Build'
parallel builders
