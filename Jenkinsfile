#!/usr/bin/env groovy

pipeline {
	agent {
		docker {
			image 'debian:9'
			args '-u 0'
		}
	}
	environment {
		CI = 'true'
		DEBIAN_FRONTEND = 'noninteractive'
	}
	stages {
		stage('Bootstrap') {
			steps {
				echo 'Bootstrapping'
				sh 'apt-get update && apt-get install -y pylint3'
			}
		}
		stage('Lint') {
			steps {
				echo 'Linting..'
				sh 'pylint3 swig/python/kopano/kopano > pylint.log || exit 0'
				recordIssues tool: pyLint(pattern: 'pylint.log'), qualityGates: [[threshold: 1, type: 'TOTAL', unstable: true]]
			}
		}
	}
}
