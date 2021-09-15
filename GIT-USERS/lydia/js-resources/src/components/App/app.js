//Dependencies
import React, { Component } from 'react';
import map from 'lodash/map';
import includes from 'lodash/includes';
//Internals
import { Courses } from '../Courses/courses';
import { MenuBar } from './menubar.js';
import { COURSES } from '../../data/courses';
import { FavoriteCourses } from '../FavoriteCourses/fav_courses';
import FilterPane from '../FilterPane/filterpane';

export default class App extends Component {
  constructor() {
    super();
    this.state = {
      levels: [],
      frameworks: [],
      types: [],
      filterPaneOptions: {
        levels: [],
        frameworks: [],
        types: [],
      },
      lengthValue: { min: 0, max: 100 },
      priceValue:  { min: 0, max: 500 },
      searchValue: '',
      dataSource: {},
      width: 0,
      device: '',
      favoriteCourses: new Set(),
      showFavorites: false,
    }
  }

  handleInputChange = (event) => {
    const name = event.target.name;
    const state = 
      name === 'levels' ? this.state.levels :
      name === 'frameworks' ? this.state.frameworks :
      name === 'types' ? this.state.types :
      null; 

    if (includes(state, event.target.value)) {
      let deleteIndex = state.indexOf(event.target.value);
      state.splice(deleteIndex, 1);
      this.setState({ [name]: state });
    } else {
      this.setState({ [name]: [...state, event.target.value] });
    }
  }

  componentDidMount() {
    const { levels, frameworks, type } = this.state;
    let source = {};
    {map(COURSES, (course) => {
      this.addToOptions(course, ['levels', 'frameworks', 'types']);
      source[course.name] = course.img;
      return this.setState({ dataSource: {...this.state.dataSource, source} });
    })}
    this.updateWindowDimensions();
    window.onresize = () => this.updateWindowDimensions();
  }

  addToOptions = (course, arr) => {
    const { filterPaneOptions } = this.state;
    map(arr, item => {
      if (!includes(filterPaneOptions[item], (course[item.slice(0, -1)]))) {
        filterPaneOptions[item] = [...filterPaneOptions[item], course[item.slice(0, -1)]];
      }
    });
    this.setState({ filterPaneOptions });
  }

  changeRangeValue = (name, value) => {
    this.setState({ [name]: value });
  }

  updateSearch = (event) => {
    event.preventDefault();
    this.setState({ searchValue: event.target.value });
  }

  updateWindowDimensions = () => {
    const width = window.innerWidth || 
                  document.documentElement.clientWidth ||
                  document.body.clientWidth;

    if (width > 1000) {
      this.setState({ device: 'desktop' });
    } else if (width >= 800 && width < 1000) {
      this.setState({ device: 'big-medium'});
    } else if (width >= 600 && width < 800) {
      this.setState({ device: 'medium' });
    } else if (width < 600) {
      this.setState({ device: 'phone' });
    }
  }

  addFavoriteCourse = (course) => {
    const { favoriteCourses } = this.state;
    if (favoriteCourses.has(course)) {
      favoriteCourses.delete(course);
      this.setState({ favoriteCourses });
    } else {
      favoriteCourses.add(course);
      this.setState({ favoriteCourses });
    }
  }

  toggleFavorites = () => {
    this.setState({ showFavorites: !this.state.showFavorites });
  }

  render() {
    return (
      <div className='app'>
        <MenuBar 
          device={ this.state.device } 
          dataSource={ this.state.dataSource } 
          updateSearch={this.updateSearch}
          showFavorites={ this.state.showFavorites }
          favoriteCourses={ this.state.favoriteCourses }
          toggleFavorites={ this.toggleFavorites } />
        <div className='app-content'>
          <FavoriteCourses 
            showFavorites={this.state.showFavorites} 
            favoriteCourses={ this.state.favoriteCourses }
            device={ this.state.device }
            addFavoriteCourse={ this.addFavoriteCourse } />
          <FilterPane 
            device={ this.state.device }
            levels={ this.state.levels }
            frameworks={ this.state.frameworks } 
            types={ this.state.types }
            lengthValue={ this.state.lengthValue }
            changeRangeValue={ this.changeRangeValue }
            handleInputChange={ this.handleInputChange }
            filterPaneOptions={ this.state.filterPaneOptions }
            priceValue={ this.state.priceValue } />
          <Courses 
            addFavoriteCourse={ this.addFavoriteCourse }
            levels={ this.state.levels }
            frameworks={ this.state.frameworks }
            types={ this.state.types }
            lengthValue={ this.state.lengthValue }
            priceValue={ this.state.priceValue }
            searchValue={ this.state.searchValue }
            filterPaneOptions={ this.state.filterPaneOptions }
            favoriteCourses={ this.state.favoriteCourses } /> 
        </div>
      </div>
    );
  }
}

