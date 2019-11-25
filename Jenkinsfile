#!/usr/bin/env groovy

pipeline {
    agent {
        dockerfile {
            filename 'Dockerfile.build'
        }
    }
    stages {
        stage('Lint') {
            steps {
                echo 'Linting..'
                sh 'pylint3 swig/python/kopano/kopano > pylint.log || exit 0'
                recordIssues tool: pyLint(pattern: 'pylint.log'), qualityGates: [[threshold: 1, type: 'TOTAL', unstable: true]]
            }
        }
        stage('Check') {
            steps {
                echo 'Checking..'
                sh 'make check'
            }
        }
    }
}
